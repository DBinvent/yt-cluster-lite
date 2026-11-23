/* SPDX-License-Identifier: MIT - Copyright (c) 2026 DBinvent */
/*
 * yt_cluster.c - YT PostgreSQL Extension
 *
 * PostgreSQL extension for YT Cluster
 * C implementation following https://www.postgresql.org/docs/current/xfunc-c.html
 */

#include "yt_cluster.h"

#include "access/htup_details.h"
#include "access/heapam.h"
#include "access/xact.h"
#include "access/xlog.h"
#include "catalog/pg_type.h"
#include "commands/dbcommands.h"
#include "commands/extension.h"
#include "commands/trigger.h"
#include "executor/spi.h"
#include "access/genam.h"
#include "access/skey.h"
#include "access/stratnum.h"
#include "pgtime.h"
#include "storage/lmgr.h"
#include "utils/snapmgr.h"
#include "utils/datum.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/bytea.h"
#include "utils/float.h"
#include "utils/lsyscache.h"
#include "utils/guc.h"
#include "utils/json.h"
#include "utils/rel.h"
#include "utils/syscache.h"
#include "nodes/bitmapset.h"
#include "nodes/parsenodes.h"
#include "tcop/utility.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PostgreSQL module magic */
PG_MODULE_MAGIC;

/* Global configuration */
YtClusterConfig g_yt_cluster_config = {
    .server_path = "",
    .loaded = false,
    .connected = false,
    .last_error = "",
    .mutex = PTHREAD_MUTEX_INITIALIZER
};

/* Endless loop prevention - session-level flag */
static bool g_in_trigger = false;

/*
 * Auto-complete: when set, the extension calls yt_complete(true) itself at
 * the transaction's PRE_COMMIT (and yt_complete(false) at ABORT) whenever the
 * transaction produced at least one yt_synchronize() modify. This removes the
 * need for the client to issue `SELECT yt_complete(true)` or to route through
 * PgBouncerXA for its COMMIT injection. GUC: yt_cluster.auto_complete (default on).
 */
static bool g_auto_complete = true;
/* has the current transaction sent at least one modify to the YT server? */
static bool g_txn_dirty = false;
/*
 * A member holds a transaction's rows and only ever commits or rolls back all of them:
 * nothing undoes one write there. So a savepoint rolled back over a write it sent would leave the
 * row on every member and not here, for good. Such a transaction cannot commit. The newest
 * subtransaction that sent one is enough to know it: any later one still open when an earlier one
 * aborts is a descendant of it.
 */
static SubTransactionId g_write_subid = InvalidSubTransactionId;
static bool g_write_undone = false;
/* GUC yt_cluster.sync_delay_ms - sleep this many ms at the end of each
 * yt_synchronize() before returning to the executor. A client can then not
 * submit modifies faster than the leader's serialized process_call consumer can
 * drain them (unthrottled pgbench overruns it: the cross-node apply times out in
 * the background, the COMMIT has already returned, the write is silently lost on
 * the peer). 0 (default) = off. Set per-session or in postgresql.conf. */
static int g_sync_delay_ms = 0;
/* GUC yt_cluster.delta - this session's statements are deltas (`set n = n + 1`,
 * a json or a text patch made where the SQL is generated): a row a concurrent write
 * changed is retryable for it. ytserv refuses such a row 55409, "do not retry; review
 * the row" - a plain UPDATE run again overwrites the other writer's change, unseen -
 * and with this on the refusal reaches the client as 40001, the code drivers retry:
 * the retry recomputes the delta from the row as it is then. Off by default. */
static bool g_delta = false;

/* The ProcessUtility hook before this extension's, and the gid of the
 * PREPARE TRANSACTION running in this session - its PRE_PREPARE reads it */
static ProcessUtility_hook_type prev_ProcessUtility = NULL;
static char g_xa_gid[200] = "";

/*
 * The transaction's isolation level as the wire names it (db.proto PgIso, whose
 * numbering differs from XactIsoLevel's): ytserv runs the party's synchronous
 * modify at it - a serializable writer's conflicts with a member's local
 * transactions are caught there too - and the apply at READ COMMITTED.
 */
static PgIso
yt_iso_level(void)
{
    switch (XactIsoLevel)
    {
        case XACT_SERIALIZABLE:
            return PG_ISO_SERIALIZABLE;
        case XACT_REPEATABLE_READ:
            return PG_ISO_REPEATABLE_READ;
        case XACT_READ_UNCOMMITTED:
            return PG_ISO_READ_UNCOMMITTED;
        default:
            return PG_ISO_READ_COMMITTED;
    }
}

/*
 * yt_info('<prefix><text>'): ytserv's one-row answer (palloc'd), or NULL
 * when it could not be asked - the reason in *err. Never raises.
 */
static char *
yt_xa_ask(const char *prefix, const char *text, const char **err)
{
    InfoRequest  req;
    InfoResponse resp;
    char        *v = NULL;
    int          ret;

    memset(&req, 0, sizeof(req));
    req.input = psprintf("%s%s", prefix, text);
    memset(&resp, 0, sizeof(resp));
    ret = yt_cluster_grpc_info(&req, &resp);
    pfree(req.input);
    if (ret != 0)
    {
        *err = yt_cluster_grpc_last_error();
        return NULL;
    }
    if (resp.data_count >= 1 && resp.data[0].value != NULL)
        v = pstrdup(resp.data[0].value);
    else
        *err = "ytserv answered no value";
    yt_cluster_pb_free_info_response(&resp);
    return v;
}

static void ensure_connected(const char *func_name);

/* [ycoord] yt_xa_ask for ycoord.c */
char *
yt_cluster_ask(const char *prefix, const char *text, const char **err)
{
    return yt_xa_ask(prefix, text, err);
}

/*
 * A client's own two-phase commit: the gid of a PREPARE TRANSACTION, for its PRE_PREPARE; and a COMMIT
 * PREPARED or ROLLBACK PREPARED - from any session - finishes the cluster side
 * first. A COMMIT PREPARED the cluster could not commit runs as ROLLBACK PREPARED,
 * and the client is told; with ytserv unreachable it is refused and the prepared
 * transaction stays, for the client to finish later. ytserv's own sessions
 * (finishing their party's transactions) are left alone, as everywhere.
 */
static void
yt_cluster_ProcessUtility(PlannedStmt *pstmt, const char *queryString, bool readOnlyTree,
                      ProcessUtilityContext context, ParamListInfo params,
                      QueryEnvironment *queryEnv, DestReceiver *dest, QueryCompletion *qc)
{
    Node *parsetree = pstmt->utilityStmt;
    bool  rolled_back = false;
    char *why = NULL;
    char  gid[200] = "";

    if (IsA(parsetree, TransactionStmt) && g_auto_complete && !yt_cluster_is_endless_loop())
    {
        TransactionStmt *stmt = (TransactionStmt *) parsetree;

        if (stmt->kind == TRANS_STMT_PREPARE && stmt->gid != NULL)
        {
            /* on a transaction already failed it prepares nothing, and its abort has come and gone:
             * the name would go with a later transaction's abort, and roll back another's */
            if (!IsAbortedTransactionBlockState())
                strlcpy(g_xa_gid, stmt->gid, sizeof(g_xa_gid));
        }
        else if ((stmt->kind == TRANS_STMT_COMMIT_PREPARED || stmt->kind == TRANS_STMT_ROLLBACK_PREPARED)
                 && stmt->gid != NULL)
        {
            bool        commit = stmt->kind == TRANS_STMT_COMMIT_PREPARED;
            const char *err = NULL;
            char       *v;

            /* Only where the extension is. Loaded by shared_preload_libraries,
             * the hook is in every backend of the instance: another database's own
             * two-phase commit has no cluster side, and must not wait on ytserv - nor
             * fail when ytserv is down. A catalog read, so only on this rare path. */
            if (!OidIsValid(get_extension_oid("yt_cluster", true)))
                goto standard;

            strlcpy(gid, stmt->gid, sizeof(gid));
            ensure_connected(commit ? "COMMIT PREPARED" : "ROLLBACK PREPARED");
            v = yt_xa_ask(commit ? "xa+:" : "xa-:", gid, &err);
            if (v == NULL)
                ereport(ERROR,
                        (errcode(ERRCODE_CONNECTION_FAILURE),
                         errmsg("yt_cluster: %s \"%s\": the cluster could not be asked: %s",
                                commit ? "COMMIT PREPARED" : "ROLLBACK PREPARED", gid, err),
                         errhint("The prepared transaction stays: finish it once ytserv answers.")));
            if (commit && strncmp(v, "failed", 6) == 0)
            {
                /* the cluster could not commit it: the local side rolls back */
                if (readOnlyTree)
                {
                    pstmt = copyObject(pstmt);
                    stmt = (TransactionStmt *) pstmt->utilityStmt;
                }
                stmt->kind = TRANS_STMT_ROLLBACK_PREPARED;
                rolled_back = true;
                why = v;
            }
        }
    }
standard:
    if (prev_ProcessUtility)
        prev_ProcessUtility(pstmt, queryString, readOnlyTree, context, params, queryEnv, dest, qc);
    else
        standard_ProcessUtility(pstmt, queryString, readOnlyTree, context, params, queryEnv, dest, qc);
    if (rolled_back)
        ereport(ERROR,
                (errcode(ERRCODE_TRANSACTION_ROLLBACK),
                 errmsg("yt_cluster: the cluster could not commit prepared transaction \"%s\" - rolled back here too: %s",
                        gid, why)));
}

/* Function declarations */
void _PG_init(void);
static void yt_cluster_xact_callback(XactEvent event, void *arg);
static void yt_cluster_subxact_callback(SubXactEvent event, SubTransactionId mySubid,
                                        SubTransactionId parentSubid, void *arg);
void _PG_fini(void);
static int yt_cluster_error_sqlstate(const char *msg, const char **rest);
static const char *yt_cluster_error_kb(const char *msg);

PG_FUNCTION_INFO_V1(yt_synchronize);
PG_FUNCTION_INFO_V1(yt_complete);
PG_FUNCTION_INFO_V1(yt_info);
PG_FUNCTION_INFO_V1(yt_info_def);
PG_FUNCTION_INFO_V1(yt_info_data);
PG_FUNCTION_INFO_V1(yt_info_data_version);
PG_FUNCTION_INFO_V1(yt_merge);
PG_FUNCTION_INFO_V1(yt_merge_force);
PG_FUNCTION_INFO_V1(yt_merge_def);
PG_FUNCTION_INFO_V1(yt_ct_encode);
PG_FUNCTION_INFO_V1(yt_ct_decode);
PG_FUNCTION_INFO_V1(yt_s3_blob);
PG_FUNCTION_INFO_V1(yt_s3_probe);

/* External function for primary key detection */
extern Bitmapset *get_primary_key_attnos(Oid relid, bool deferrableOk, Oid *constraintOid);

/*
 * Module initialization
 */
void
_PG_init(void)
{
    pthread_mutex_init(&g_yt_cluster_config.mutex, NULL);

    DefineCustomBoolVariable("yt_cluster.auto_complete",
                             "Call yt_complete() automatically at transaction commit/abort.",
                             "When on, a transaction that fired yt_synchronize() is completed "
                             "into the cluster on its own COMMIT, with no explicit yt_complete() "
                             "call and no PgBouncerXA COMMIT injection.",
                             &g_auto_complete,
                             true,
                             PGC_USERSET,
                             0,
                             NULL, NULL, NULL);

    DefineCustomIntVariable("yt_cluster.sync_delay_ms",
                            "Delay (ms) at the end of each yt_synchronize() before returning.",
                            "Throttles a single client so it cannot submit cluster modifies "
                            "faster than the leader's serialized consumer can apply them. "
                            "0 disables it.",
                            &g_sync_delay_ms,
                            0,
                            0,
                            60000,
                            PGC_USERSET,
                            GUC_UNIT_MS,
                            NULL, NULL, NULL);

    DefineCustomBoolVariable("yt_cluster.delta",
                             "This session's statements are deltas: a conflicting row is retryable.",
                             "When on, a row a concurrent write changed - refused 55409, do not "
                             "retry - reaches the client as 40001, the code drivers retry: the "
                             "statement recomputes its change from the row as it is then. Off: a "
                             "plain rollback, the user reviews the row.",
                             &g_delta,
                             false,
                             PGC_USERSET,
                             0,
                             NULL, NULL, NULL);

    RegisterXactCallback(yt_cluster_xact_callback, NULL);
    /* [ycoord] a transaction's cluster locks go at its end */
    yt_ycoord_init();
    /* A savepoint rolled back past a failed trigger call */
    RegisterSubXactCallback(yt_cluster_subxact_callback, NULL);
    /* A client's PREPARE TRANSACTION / COMMIT PREPARED / ROLLBACK PREPARED */
    prev_ProcessUtility = ProcessUtility_hook;
    ProcessUtility_hook = yt_cluster_ProcessUtility;

    elog(LOG, "yt_cluster: extension loaded (version %s)", YT_CLUSTER_VERSION);
}

/*
 * Module cleanup
 */
void
_PG_fini(void)
{
    UnregisterXactCallback(yt_cluster_xact_callback, NULL);
    UnregisterSubXactCallback(yt_cluster_subxact_callback, NULL);
    ProcessUtility_hook = prev_ProcessUtility;
    yt_cluster_grpc_disconnect();
    pthread_mutex_destroy(&g_yt_cluster_config.mutex);
    elog(LOG, "yt_cluster: extension unloaded");
}

/*
 * The SQLSTATE a ytserv refusal carries for the client.
 *
 * ytserv writes it as `<code>: ` at POSITION 0 of the message
 * (`55000: recon: row [3] of ... is blocked ...`)
 * and this reads it there and nowhere else: a code further in is prose - a
 * quoted PostgreSQL error, a uuid fragment - and must not be promoted to the
 * field a driver branches on. Five of [0-9A-Z], at least one digit (every
 * SQLSTATE class has one, so `ABORT: ` stays a word), then ": ".
 *
 * No code -> ERRCODE_CONNECTION_FAILURE (08006), as every error was before, and
 * *rest is the whole message. With one, *rest points past "XXXXX: " so the code
 * is not printed twice. Until this existed every refusal - "this row needs an
 * audit", "the cluster is short of nodes", "the socket dropped" - reached the
 * client as 08006 and no driver could tell whether to retry.
 */
/*
 * At the rollback point: a row a concurrent write changed (55409) is
 * retryable when the session says its statements are deltas (yt_cluster.delta) -
 * 40001, its advice turned round. Any other code, or a session that said nothing,
 * stays as ytserv sent it: a plain rollback, no retry.
 */
static int
yt_cluster_delta_retry(int code, const char **rest)
{
    static const char advice[] = "refused, do not retry; review the row";
    const char *at;

    if (!g_delta || code != MAKE_SQLSTATE('5', '5', '4', '0', '9'))
        return code;
    at = strstr(*rest, advice);
    if (at == NULL)
        *rest = psprintf("%s - a delta (yt_cluster.delta): retry the transaction", *rest);
    else
        *rest = psprintf("%.*sa delta (yt_cluster.delta): retry the transaction%s",
                         (int) (at - *rest), *rest, at + strlen(advice));
    return ERRCODE_T_R_SERIALIZATION_FAILURE;
}

static int
yt_cluster_error_sqlstate(const char *msg, const char **rest)
{
    int i;
    int digits = 0;

    *rest = msg ? msg : "";
    if (msg == NULL)
        return ERRCODE_CONNECTION_FAILURE;
    for (i = 0; i < 5; i++)
    {
        char c = msg[i];    /* a shorter message stops at its '\0' here */

        if (c >= '0' && c <= '9')
            digits++;
        else if (c < 'A' || c > 'Z')
            return ERRCODE_CONNECTION_FAILURE;
    }
    if (digits == 0 || msg[5] != ':' || msg[6] != ' ')
        return ERRCODE_CONNECTION_FAILURE;
    *rest = msg + 7;
    return yt_cluster_delta_retry(MAKE_SQLSTATE(msg[0], msg[1], msg[2], msg[3], msg[4]), rest);
}

/*
 * The page of the error code a ytserv message starts with - after its SQLSTATE, which
 * yt_cluster_error_sqlstate took off - for the HINT a client shows: "YT-E1820 ..." gives
 * "https://docs.dbinvent.com/errors/YT-E1820" (what it means, how to fix it, a link to report
 * it). A code is 2 to 4 capitals, "-E", 3 or 4 digits and a space, as every product writes
 * its own (YT, SGE, SG, RPPD, ETCD, DD). NULL for a message without one.
 */
static const char *
yt_cluster_error_kb(const char *msg)
{
    int letters = 0;
    int digits = 0;

    if (msg == NULL)
        return NULL;
    while (letters < 4 && msg[letters] >= 'A' && msg[letters] <= 'Z')
        letters++;
    if (letters < 2 || msg[letters] != '-' || msg[letters + 1] != 'E')
        return NULL;
    while (digits < 4 && msg[letters + 2 + digits] >= '0' && msg[letters + 2 + digits] <= '9')
        digits++;
    if (digits < 3 || msg[letters + 2 + digits] != ' ')
        return NULL;
    return psprintf("https://docs.dbinvent.com/errors/%.*s", letters + 2 + digits, msg);
}

/*
 * Transaction boundary hook. Finishes the cluster 2PC that yt_synchronize()
 * started, so ordinary clients need neither an explicit yt_complete() nor
 * PgBouncerXA. No-op unless this transaction actually sent a modify
 * (g_txn_dirty) and yt_cluster.auto_complete is on.
 */
/*
 * Make this transaction's WAL - and with it its transaction id -
 * durable before the cluster commits its rows anywhere else.
 *
 * After a crash, ytserv decides this node's own transactions by
 * txid_status(): committed here, or lost to the restart and to be applied from
 * the cluster WAL (reapply_own_lost, resolve_own_in_doubt). That answer is only
 * sound for an id PostgreSQL's recovery knows. A transaction whose records were
 * still in the WAL buffers when the server was killed leaves no trace, recovery
 * sets the next id below it, and the id is handed out again - to a new
 * transaction that commits. txid_status() then said "committed" for a
 * transaction that never committed here, the recovery skipped it, and the origin
 * lacked a row every other node held (seen in testing: an origin killed after
 * yt_complete(true) under load, 3 of 3 runs; xid 5919 reported committed, the row
 * absent).
 *
 * One flush up to this transaction's last record: what COMMIT would flush
 * anyway, done before the cluster's commit instead of after it.
 */
static void
yt_cluster_wal_durable(void)
{
    if (XactLastRecEnd != InvalidXLogRecPtr)
        XLogFlush(XactLastRecEnd);
}

/*
 * The endless-loop flag lives no longer than the trigger call that set it,
 * and so no longer than its transaction: an ERROR between yt_synchronize() setting it and
 * clearing it - a cancel, a lock timeout, a failed lookup - unwound past the clearing and
 * left it set for the session's life. Every later write on that backend was then taken for
 * the trigger's own and not synchronized: committed on this node alone, with no error, and
 * its transaction's end skipped the cluster's completion too (a write through ytserv's
 * pooled connection, seen in testing). Cleared at every end of a
 * transaction or a subtransaction: no trigger call is running then.
 */
static void
yt_cluster_trigger_flag_clear(void)
{
    g_in_trigger = false;
}

static void
yt_cluster_subxact_callback(SubXactEvent event, SubTransactionId mySubid,
                            SubTransactionId parentSubid, void *arg)
{
    (void) parentSubid;
    (void) arg;
    if (event == SUBXACT_EVENT_ABORT_SUB)
    {
        yt_cluster_trigger_flag_clear();
        /* a savepoint rolled back over a write this transaction sent */
        if (g_write_subid != InvalidSubTransactionId && g_write_subid >= mySubid)
            g_write_undone = true;
    }
}

/* A transaction that took back a write it sent stops here, and its abort rolls the party back */
static void
yt_cluster_refuse_undone(void)
{
    if (g_write_undone)
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg("yt_cluster: a savepoint was rolled back over a replicated write: the other nodes cannot undo it, so this transaction cannot commit"),
                 errhint("Roll back and retry the transaction without rolling back over a write.")));
}

static void
yt_cluster_xact_callback(XactEvent event, void *arg)
{
    /* before the dirty check: a transaction whose first modify failed
     * inside the trigger is not dirty, and its end must clear the flag all the same;
     * before the ABORT branch below too, which asks the flag whether to roll the
     * party back */
    if (event == XACT_EVENT_ABORT || event == XACT_EVENT_PARALLEL_ABORT
        || event == XACT_EVENT_COMMIT || event == XACT_EVENT_PARALLEL_COMMIT
        || event == XACT_EVENT_PREPARE)
    {
        yt_cluster_trigger_flag_clear();
        g_write_subid = InvalidSubTransactionId;
        g_write_undone = false;
    }

    ClusterComplete req;
    CompleteReply   reply;
    int             ret;

    (void) arg;

    if (!g_txn_dirty)
    {
        /* no cluster side: a name a PREPARE left does not outlive its transaction */
        if (event == XACT_EVENT_ABORT || event == XACT_EVENT_PARALLEL_ABORT
            || event == XACT_EVENT_COMMIT || event == XACT_EVENT_PARALLEL_COMMIT
            || event == XACT_EVENT_PREPARE)
            g_xa_gid[0] = '\0';
        return;
    }

    switch (event)
    {
        case XACT_EVENT_PRE_COMMIT:
        case XACT_EVENT_PARALLEL_PRE_COMMIT:
        {
            FullTransactionId txid;

            /* The first phase of the cluster 2PC. Do NOT clear g_txn_dirty
             * here: XACT_EVENT_COMMIT (or _ABORT) must still fire to finalize,
             * and the guard at the top of this function would swallow it. */

            /* ahead of the way out below: a write the cluster refused marks the
             * connection down, and a handler that caught it would commit here alone */
            if (g_auto_complete && !yt_cluster_is_endless_loop())
                yt_cluster_refuse_undone();

            if (!g_auto_complete || yt_cluster_is_endless_loop() || !g_yt_cluster_config.connected)
            {
                g_txn_dirty = false;
                return;
            }

            txid = GetCurrentFullTransactionIdIfAny();
            if (!FullTransactionIdIsValid(txid))
            {
                g_txn_dirty = false;
                return;
            }

            /* see yt_cluster_wal_durable() */
            yt_cluster_wal_durable();

            memset(&req, 0, sizeof(req));
            req.txid = U64FromFullTransactionId(txid);
            req.commit = COMMIT_PREPARE;
            memset(&reply, 0, sizeof(reply));

            ret = yt_cluster_grpc_complete(&req, &reply);
            if (ret != 0)
            {
                const char *msg;
                const char *kb;
                int         code;

                /* A cancel ended the call: raise the cancel, with the
                 * connection left up so the abort still rolls the party back. */
                CHECK_FOR_INTERRUPTS();

                /* PRE_COMMIT: raising here aborts the local commit, which is
                 * correct - if the cluster will not accept the completion the
                 * transaction must not be reported as committed. The abort
                 * fires XACT_EVENT_ABORT below, which rolls the party back. */
                pthread_mutex_lock(&g_yt_cluster_config.mutex);
                g_yt_cluster_config.connected = false;
                pthread_mutex_unlock(&g_yt_cluster_config.mutex);
                code = yt_cluster_error_sqlstate(yt_cluster_grpc_last_error(), &msg);
                kb = yt_cluster_error_kb(msg);
                ereport(ERROR,
                        (errcode(code),
                         errmsg("yt_cluster auto-complete (prepare): %s", msg),
                         kb ? errhint("%s", kb) : 0));
            }
            return;
        }

        case XACT_EVENT_ABORT:
        case XACT_EVENT_PARALLEL_ABORT:
        {
            FullTransactionId txid;

            g_txn_dirty = false;

            if (!g_auto_complete || yt_cluster_is_endless_loop() || !g_yt_cluster_config.connected)
            {
                g_xa_gid[0] = '\0';
                return;
            }
            /* a PREPARE TRANSACTION that failed here after the cluster prepared:
             * its record goes with the party's rollback (below) */
            if (g_xa_gid[0] != '\0')
            {
                const char *err = NULL;
                (void) yt_xa_ask("xa-:", g_xa_gid, &err);
                g_xa_gid[0] = '\0';
            }

            txid = GetCurrentFullTransactionIdIfAny();
            if (!FullTransactionIdIsValid(txid))
                return;

            memset(&req, 0, sizeof(req));
            req.txid = U64FromFullTransactionId(txid);
            req.commit = COMMIT_ROLLBACK;
            memset(&reply, 0, sizeof(reply));

            /* Best effort: never raise from an abort path. */
            ret = yt_cluster_grpc_complete(&req, &reply);
            if (ret != 0)
                elog(WARNING, "yt_cluster auto-complete (rollback): %s", yt_cluster_grpc_last_error());
            return;
        }

        case XACT_EVENT_COMMIT:
        case XACT_EVENT_PARALLEL_COMMIT:
        {
            FullTransactionId txid;

            /* The second phase. The local commit record is already written and
             * durable, so this transaction IS committed: the cluster side must
             * now COMMIT PREPARED. Two hard rules here:
             *  - never raise: an ERROR after commit is a PANIC ("cannot abort
             *    transaction, it was already committed"), so failures are logged
             *    and left to the WAL / data_version catch-up, which can only
             *    leave a peer BEHIND (repairable), never ahead.
             *  - if nothing was prepared on the cluster the server answers ok;
             *    it is a dummy local UDS call, not an error. */
            g_txn_dirty = false;

            if (!g_auto_complete || yt_cluster_is_endless_loop() || !g_yt_cluster_config.connected)
                return;

            txid = GetCurrentFullTransactionIdIfAny();
            if (!FullTransactionIdIsValid(txid))
                return;

            memset(&req, 0, sizeof(req));
            req.txid = U64FromFullTransactionId(txid);
            req.commit = COMMIT_COMMIT;
            memset(&reply, 0, sizeof(reply));

            ret = yt_cluster_grpc_complete(&req, &reply);
            if (ret != 0)
                elog(WARNING, "yt_cluster auto-complete (commit prepared): %s", yt_cluster_grpc_last_error());
            return;
        }

        case XACT_EVENT_PRE_PREPARE:
        {
            /* A client's own two-phase commit:
             * its COMMIT PREPARED runs later, maybe in another session, so the
             * cluster side is prepared now - the party holds the transaction - and
             * recorded under the client's gid, before PostgreSQL prepares the local
             * side. Refused, the local PREPARE fails and its abort rolls the party
             * back. The COMMIT PREPARED (yt_cluster_ProcessUtility) finishes both. */
            FullTransactionId txid;
            const char *err = NULL;
            char       *v;

            if (g_auto_complete && !yt_cluster_is_endless_loop())
                yt_cluster_refuse_undone();
            if (!g_auto_complete || yt_cluster_is_endless_loop() || !g_yt_cluster_config.connected)
                return;
            txid = GetCurrentFullTransactionIdIfAny();
            if (!FullTransactionIdIsValid(txid) || g_xa_gid[0] == '\0')
                return;
            v = yt_xa_ask("xa=:", psprintf(UINT64_FORMAT ":%s", U64FromFullTransactionId(txid), g_xa_gid), &err);
            if (v == NULL || strcmp(v, "ok") != 0)
                ereport(ERROR,
                        (errcode(ERRCODE_TRANSACTION_ROLLBACK),
                         errmsg("yt_cluster: the cluster did not prepare transaction \"%s\": %s",
                                g_xa_gid, v != NULL ? v : err)));
            return;
        }

        case XACT_EVENT_PREPARE:
            /* prepared on the cluster and here: its COMMIT PREPARED finishes it */
            g_txn_dirty = false;
            g_xa_gid[0] = '\0';
            return;

        default:
            return;
    }
}

/*
 * Get GUC variable as string
 */
char *
yt_cluster_get_guc_string(const char *name)
{
    const char *val = GetConfigOption(name, true, false);
    if (val == NULL)
        return NULL;
    return pstrdup(val);
}

/*
 * Check for endless loop
 */
bool
yt_cluster_is_endless_loop(void)
{
    char *node = yt_cluster_get_guc_string(YT_CLUSTER_NODE_GUC);
    if (node != NULL && strcmp(node, YT_CLUSTER_LOCAL) == 0)
    {
        pfree(node);
        return true;
    }
    if (node != NULL)
        pfree(node);

    return g_in_trigger;
}

/* Column accessor functions, not used at present */
#if 0
static bool get_column_int32(HeapTuple tuple, TupleDesc tupdesc, int attnum, int32 *value);
static bool get_column_int64(HeapTuple tuple, TupleDesc tupdesc, int attnum, int64 *value);
static char *get_column_string(HeapTuple tuple, TupleDesc tupdesc, int attnum);
static bool get_column_float(HeapTuple tuple, TupleDesc tupdesc, int attnum, double *value);
static bool get_column_bool(HeapTuple tuple, TupleDesc tupdesc, int attnum, bool *value);
#endif

/*
 * Convert HeapTuple datum to VarType based on type OID
 */
/*
 * DocumentDB's document type, documentdb_core.bson: its text is the
 * session's choice (documentdb_core's JSON GUC) - canonical extended JSON, or
 * BSONHEX and the bytes in hex. A row's value of it is sent as BSONHEX always: its
 * input reads either, so every member takes the same bytes, and ytserv merges a
 * document path by path from them. Its OID is the
 * extension's, so it is found by its name and schema.
 */
#define BSONHEX_PREFIX      "BSONHEX"
#define BSONHEX_PREFIX_LEN  7

static bool
is_documentdb_bson(Oid typoid)
{
    HeapTuple    tp;
    Form_pg_type t;
    bool         is;
    Oid          nsp;
    char        *nspname;

    if (typoid < FirstNormalObjectId)
        return false;
    tp = SearchSysCache1(TYPEOID, ObjectIdGetDatum(typoid));
    if (!HeapTupleIsValid(tp))
        return false;
    t = (Form_pg_type) GETSTRUCT(tp);
    is = strcmp(NameStr(t->typname), "bson") == 0 && t->typlen == -1;
    nsp = t->typnamespace;
    ReleaseSysCache(tp);
    if (!is)
        return false;
    nspname = get_namespace_name(nsp);
    return nspname != NULL && strcmp(nspname, "documentdb_core") == 0;
}

/*
 * A value sent as its text is written under fixed settings - ISO dates,
 * the postgres interval style, hex bytea, floats exact - never the session's: a member
 * parses it under any DateStyle, and a keyless row's value is read back there as the
 * same text (ytserv reads it back the same way; its apply sessions pin the same). DateStyle's
 * order part is left as it is: ISO is read in any order.
 */
static char *
output_canonical(Oid typoid, Datum datum)
{
    Oid     out_func;
    bool    is_varlena;
    int     date_style = DateStyle;
    int     interval_style = IntervalStyle;
    int     bytea_out = bytea_output;
    int     float_digits = extra_float_digits;
    char   *s = NULL;

    getTypeOutputInfo(typoid, &out_func, &is_varlena);
    DateStyle = USE_ISO_DATES;
    IntervalStyle = INTSTYLE_POSTGRES;
    bytea_output = BYTEA_OUTPUT_HEX;
    extra_float_digits = 1;
    PG_TRY();
    {
        s = OidOutputFunctionCall(out_func, datum);
    }
    PG_FINALLY();
    {
        DateStyle = date_style;
        IntervalStyle = interval_style;
        bytea_output = bytea_out;
        extra_float_digits = float_digits;
    }
    PG_END_TRY();
    return s;
}

static VarType *
datum_to_vartype(HeapTuple tuple, TupleDesc tupdesc, int attnum, Oid typoid)
{
    VarType *var = palloc0(sizeof(VarType));
    Datum datum;
    bool isnull;

    datum = heap_getattr(tuple, attnum, tupdesc, &isnull);
    if (isnull)
    {
        var->kind = VAR_TYPE_NULL;
        return var;
    }

    switch (typoid)
    {
        case INT2OID:
            var->kind = VAR_TYPE_INT;
            var->value.int_val = DatumGetInt16(datum);
            break;
        case INT4OID:
            var->kind = VAR_TYPE_INT;
            var->value.int_val = DatumGetInt32(datum);
            break;
        case INT8OID:
            var->kind = VAR_TYPE_INT;
            var->value.int_val = DatumGetInt64(datum);
            break;
        case FLOAT4OID:
            var->kind = VAR_TYPE_FLOAT;
            var->value.float_val = DatumGetFloat4(datum);
            break;
        case FLOAT8OID:
            var->kind = VAR_TYPE_FLOAT;
            var->value.float_val = DatumGetFloat8(datum);
            break;
        case BOOLOID:
            var->kind = VAR_TYPE_BOOL;
            var->value.bool_val = DatumGetBool(datum);
            break;
        case TEXTOID:
        case VARCHAROID:
        case BPCHAROID:
            var->kind = VAR_TYPE_UNIVERSAL;
            var->value.str_val = TextDatumGetCString(datum);
            break;
        case JSONOID:
        case JSONBOID:
            var->kind = VAR_TYPE_UNIVERSAL;
            {
                Oid out_func;
                bool is_varlena;
                getTypeOutputInfo(typoid, &out_func, &is_varlena);
                var->value.str_val = OidOutputFunctionCall(out_func, datum);
            }
            break;
        default:
            /* For other types, convert to string */
            var->kind = VAR_TYPE_UNIVERSAL;
            if (is_documentdb_bson(typoid))
            {
                /* BSONHEX and the document's bytes, whatever this
                 * session's text of the type is */
                bytea  *b = DatumGetByteaPP(datum);
                size_t  len = VARSIZE_ANY_EXHDR(b);
                char   *s = palloc(BSONHEX_PREFIX_LEN + 2 * len + 1);

                memcpy(s, BSONHEX_PREFIX, BSONHEX_PREFIX_LEN);
                hex_encode(VARDATA_ANY(b), len, s + BSONHEX_PREFIX_LEN);
                s[BSONHEX_PREFIX_LEN + 2 * len] = '\0';
                var->value.str_val = s;
            }
            else
                var->value.str_val = output_canonical(typoid, datum);
            break;
    }

    return var;
}

/*
 * Check if reconnection is needed
 */
static bool
needs_reconnect(const char *host)
{
    pthread_mutex_lock(&g_yt_cluster_config.mutex);

    if (!g_yt_cluster_config.connected)
    {
        pthread_mutex_unlock(&g_yt_cluster_config.mutex);
        return true;
    }

    if (strcmp(g_yt_cluster_config.server_path, host) != 0)
    {
        pthread_mutex_unlock(&g_yt_cluster_config.mutex);
        return true;
    }

    pthread_mutex_unlock(&g_yt_cluster_config.mutex);
    return false;
}

/*
 * Connect to YT server.
 * Unix socket paths (starting with '/') use unix:// scheme as expected by grpc_client.c
 */
static int
do_connect(const char *host)
{
    char path[YT_CLUSTER_MAX_HOST_LEN];

    if (strstr(host, "://") != NULL)
    {
        snprintf(path, sizeof(path), "%s", host);
    }
    else if (host[0] == '/')
    {
        /* Unix socket absolute path */
        snprintf(path, sizeof(path), "unix://%s", host);
    }
    else
    {
        snprintf(path, sizeof(path), "http://%s", host);
    }

    return yt_cluster_grpc_connect(path);
}

/* Forward declaration */
static char *load_socket_path(void);

/*
 * Ensure connection to YT server is established.
 * Calls ereport(ERROR) on failure - never returns in that case.
 */
static void
ensure_connected(const char *func_name)
{
    char *socket_path = load_socket_path();

    if (needs_reconnect(socket_path))
    {
        if (do_connect(socket_path) != 0)
        {
            char *path_copy = pstrdup(socket_path);
            pfree(socket_path);
            ereport(ERROR,
                    (errcode(ERRCODE_CONNECTION_FAILURE),
                     errmsg("%s: Not connected to YT server via unix socket \"%s\": %s",
                            func_name, path_copy, yt_cluster_grpc_last_error())));
        }

        pthread_mutex_lock(&g_yt_cluster_config.mutex);
        strncpy(g_yt_cluster_config.server_path, socket_path, YT_CLUSTER_MAX_HOST_LEN - 1);
        g_yt_cluster_config.loaded = true;
        g_yt_cluster_config.connected = true;
        pthread_mutex_unlock(&g_yt_cluster_config.mutex);
    }

    pfree(socket_path);
}

/* the same connection, for yt_llm_query() in llm.c */
void
yt_cluster_ensure_connected(const char *func_name)
{
    ensure_connected(func_name);
}

/*
 * Load Unix socket path from config: {unix_socket_directories}/.s.YT.{port}
 * Matches the socket path ytserv listens on (the ".s.YT." name).
 */
static char *
load_socket_path(void)
{
    const char *usd_raw = GetConfigOption("unix_socket_directories", true, false);
    const char *port_str = GetConfigOption("port", true, false);
    char *usd;
    char *comma;

    if (usd_raw == NULL)
        usd_raw = "/var/run/postgresql";
    if (port_str == NULL)
        port_str = "5432";

    /* Take first directory if multiple are specified (comma-separated) */
    usd = pstrdup(usd_raw);
    comma = strchr(usd, ',');
    if (comma != NULL)
        *comma = '\0';
    /* Trim trailing whitespace */
    {
        int len = strlen(usd);
        while (len > 0 && usd[len - 1] == ' ')
            usd[--len] = '\0';
    }

    return psprintf("%s/.s.YT.%s", usd, port_str);
}

/*
 * Get primary keys for relation
 */
static Bitmapset *
get_pk_bitmapset(Relation rel)
{
    Oid constraint_oid = InvalidOid;
    return get_primary_key_attnos(RelationGetRelid(rel), false, &constraint_oid);
}

/*
 * Format InfoResponse as JSON string
 */
/*
 * Append a string as a JSON string BODY (no surrounding quotes), escaping what
 * RFC 8259 requires.
 *
 * yt_info values are not tame: the schema gate returns diff hunks that are
 * themselves JSON and a YAML fragment full of newlines, the placement catalog
 * returns a JSON object, a table's `filter` is SQL an operator wrote and
 * routinely contains quotes, and alert details carry arbitrary text. Splicing
 * any of those in with a bare %s produced output that no JSON parser would
 * accept - silently, because the usual way to look at yt_info is to read it.
 *
 * Bytes >= 0x80 pass through unchanged: PostgreSQL text here is UTF-8 and JSON
 * takes UTF-8 directly, so re-encoding them would be wrong as well as lossy.
 */
static void
append_json_escaped(StringInfo out, const char *s)
{
    if (s == NULL)
        return;

    for (; *s; s++)
    {
        unsigned char c = (unsigned char) *s;

        switch (c)
        {
            case '"':  appendStringInfoString(out, "\\\""); break;
            case '\\': appendStringInfoString(out, "\\\\"); break;
            case '\n': appendStringInfoString(out, "\\n");  break;
            case '\r': appendStringInfoString(out, "\\r");  break;
            case '\t': appendStringInfoString(out, "\\t");  break;
            case '\b': appendStringInfoString(out, "\\b");  break;
            case '\f': appendStringInfoString(out, "\\f");  break;
            default:
                if (c < 0x20)
                    appendStringInfo(out, "\\u%04x", c);
                else
                    appendStringInfoChar(out, (char) c);
        }
    }
}

char *
yt_cluster_info_response_to_json(const InfoResponse *resp)
{
    StringInfoData json;
    int i;

    initStringInfo(&json);
    appendStringInfoChar(&json, '[');

    for (i = 0; i < resp->data_count; i++)
    {
        if (i > 0)
            appendStringInfoChar(&json, ',');

        appendStringInfoString(&json, "{\"name\":\"");
        append_json_escaped(&json, resp->data[i].name);
        appendStringInfoString(&json, "\",\"value\":\"");
        append_json_escaped(&json, resp->data[i].value);
        appendStringInfoChar(&json, '"');

        if (resp->data[i].module != NULL)
        {
            appendStringInfoString(&json, ",\"module\":\"");
            append_json_escaped(&json, resp->data[i].module);
            appendStringInfoChar(&json, '"');
        }

        appendStringInfoChar(&json, '}');
    }

    appendStringInfoChar(&json, ']');

    return json.data;
}

/*
 * Trigger function: yt_synchronize
 */
Datum
yt_synchronize(PG_FUNCTION_ARGS)
{
    SubTransactionId write_subid_before;
    TriggerData    *trigdata = (TriggerData *) fcinfo->context;
    HeapTuple       rettuple;
    HeapTuple       current_tuple;
    HeapTuple       new_tuple;
    TupleDesc       tupdesc;
    Relation        rel;
    char           *table_name;
    char           *schema_name;
    char           *db_name;
    DbAction        action;
    DbInputEntity   entity;
    ModifyReply     reply;
    Bitmapset      *pk_attrs;
    bool            keyless;
    int             attnum;
    int             natts;
    char           *socket_path;
    int             ret;
    FullTransactionId txid;

    /* Verify this is being called as a trigger */
    if (!CALLED_AS_TRIGGER(fcinfo))
        elog(ERROR, "yt_synchronize: not called as trigger");

    /* Check for endless loop */
    if (yt_cluster_is_endless_loop())
    {
        if (TRIGGER_FIRED_BY_DELETE(trigdata->tg_event))
            return PointerGetDatum(trigdata->tg_trigtuple);
        else
            return PointerGetDatum(trigdata->tg_newtuple ? trigdata->tg_newtuple : trigdata->tg_trigtuple);
    }

    /* Set endless loop prevention flag */
    g_in_trigger = true;

    /* Get trigger context */
    rel = trigdata->tg_relation;
    tupdesc = RelationGetDescr(rel);
    natts = tupdesc->natts;

    /* Get current tuple */
    if (TRIGGER_FIRED_BY_DELETE(trigdata->tg_event))
    {
        current_tuple = trigdata->tg_trigtuple;
        new_tuple = NULL;
        rettuple = trigdata->tg_trigtuple;
    }
    else if (TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event))
    {
        current_tuple = trigdata->tg_trigtuple;
        new_tuple = trigdata->tg_newtuple;
        rettuple = trigdata->tg_newtuple;
    }
    else
    {
        current_tuple = trigdata->tg_newtuple ? trigdata->tg_newtuple : trigdata->tg_trigtuple;
        new_tuple = NULL;
        rettuple = current_tuple;
    }

    /* Determine action type */
    if (TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event))
        action = DB_ACTION_UPDATE;
    else if (TRIGGER_FIRED_BY_INSERT(trigdata->tg_event))
        action = DB_ACTION_INSERT;
    else if (TRIGGER_FIRED_BY_DELETE(trigdata->tg_event))
        action = DB_ACTION_DELETE;
    else
        action = DB_ACTION_TRUNCATE;

    /* Get table info */
    table_name = RelationGetRelationName(rel);
    schema_name = get_namespace_name(RelationGetNamespace(rel));
    db_name = get_database_name(MyDatabaseId);

    /* Get primary keys */
    pk_attrs = get_pk_bitmapset(rel);
    /* A table with no primary key sent, its key being the only thing an
     * UPDATE or a DELETE carries to find its row, nothing to find it by: every member
     * ran `delete from t` - the whole table - for one row. Its
     * UPDATE and DELETE now send the whole old row in old_data - an UPDATE its changed
     * columns' new values in new_data, as before - and a member takes the one row
     * holding it (ytserv does the same); a member before that compares the row
     * too. Keyless: no key column is sent - a dropped or generated one is not. */
    keyless = true;
    for (attnum = 1; attnum <= natts; attnum++)
    {
        Form_pg_attribute attr = TupleDescAttr(tupdesc, attnum - 1);

        if (!attr->attisdropped && !attr->attgenerated &&
            bms_is_member(attnum - FirstLowInvalidHeapAttributeNumber, pk_attrs))
            keyless = false;
    }

    /* Check connection */
    socket_path = load_socket_path();
    if (needs_reconnect(socket_path))
    {
        ret = do_connect(socket_path);
        if (ret != 0)
        {
            pfree(socket_path);
            g_in_trigger = false;
            ereport(ERROR,
                    (errcode(ERRCODE_CONNECTION_FAILURE),
                     errmsg("yt_synchronize: failed to connect to YT server: %s",
                            yt_cluster_grpc_last_error())));
        }

        pthread_mutex_lock(&g_yt_cluster_config.mutex);
        strncpy(g_yt_cluster_config.server_path, socket_path, YT_CLUSTER_MAX_HOST_LEN - 1);
        g_yt_cluster_config.loaded = true;
        g_yt_cluster_config.connected = true;
        pthread_mutex_unlock(&g_yt_cluster_config.mutex);
    }
    pfree(socket_path);

    /* Get transaction ID: the TOP transaction's, the one the commit completes
     * (yt_cluster_xact_callback). In a subtransaction - a savepoint, a PL/pgSQL
     * EXCEPTION block, DocumentDB's own writes - the current one is the
     * subtransaction's: its write was sent under an id no commit ever completed, and
     * the party left it to an apply that never came. */
    txid = GetTopFullTransactionIdIfAny();

    /* Build TableRef */
    memset(&entity, 0, sizeof(entity));
    entity.tx = U64FromFullTransactionId(txid);
    entity.action = action;
    /* the origin's isolation level: the party runs its synchronous modify at it,
     * the apply stays READ COMMITTED */
    entity.iso = yt_iso_level();

    /* Build table reference */
    entity.table = palloc0(sizeof(TableRef));
    entity.table->oid = RelationGetRelid(rel);
    entity.table->db = pstrdup(db_name);
    entity.table->schema = pstrdup(schema_name);
    entity.table->table = pstrdup(table_name);
    entity.table->columns_count = 0;
    entity.table->columns = palloc0(sizeof(YtColumnRef) * natts);

    /* Build columns: the live ones. A dropped column stays in the descriptor
     * (attisdropped, named "........pg.dropped.N........", NULL in every row);
     * sent along, every member's statement named it and failed with a syntax
     * error at "..", so after one DROP COLUMN no write to the table committed
     * anywhere. Values are keyed by attnum, so
     * the rest stay addressed as before. A generated column (attgenerated) is
     * left out the same way: every member computes it from the row, and naming it
     * in an insert or an update is refused there ("cannot insert a non-DEFAULT
     * value into column"). */
    for (attnum = 1; attnum <= natts; attnum++)
    {
        Form_pg_attribute attr = TupleDescAttr(tupdesc, attnum - 1);
        YtColumnRef *col;

        if (attr->attisdropped || attr->attgenerated)
            continue;
        col = &entity.table->columns[entity.table->columns_count++];
        col->attnum = attnum;
        col->oid = attr->attrelid;
        col->name = pstrdup(NameStr(attr->attname));
        col->pk = bms_is_member(attnum - FirstLowInvalidHeapAttributeNumber, pk_attrs);
        col->atttypid = attr->atttypid;
        col->atttypmod = attr->atttypmod;
        col->ignore = false;
    }

    /* Build old_data and new_data */
    entity.old_data = palloc0(sizeof(KeyValue) * natts);
    entity.new_data = palloc0(sizeof(KeyValue) * natts);
    entity.old_data_count = 0;
    entity.new_data_count = 0;

    for (attnum = 1; attnum <= natts; attnum++)
    {
        Form_pg_attribute attr = TupleDescAttr(tupdesc, attnum - 1);
        bool is_pk = bms_is_member(attnum - FirstLowInvalidHeapAttributeNumber, pk_attrs);
        VarType *old_var = NULL;
        VarType *new_var = NULL;
        bool include_old = false;
        bool include_new = false;

        if (attr->attisdropped || attr->attgenerated)
            continue;   /* no column for it above, so no value either */

        if (current_tuple != NULL)
            old_var = datum_to_vartype(current_tuple, tupdesc, attnum, attr->atttypid);

        if (new_tuple != NULL)
            new_var = datum_to_vartype(new_tuple, tupdesc, attnum, attr->atttypid);

        if (action == DB_ACTION_DELETE)
        {
            if (is_pk)
                include_new = true;
            if (keyless)
                include_old = true;
        }
        else if (action == DB_ACTION_INSERT)
        {
            include_new = true;
        }
        else if (action == DB_ACTION_UPDATE)
        {
            /* Include if changed or if PK. A PK column always goes in new_data;
             * a CHANGED one goes in old_data too: the receivers find the row by
             * the key in old_data (DbInputEntity::pk prefers it), and an UPDATE of
             * the key used to carry only the new value - every receiver looked the
             * row up under a key it did not have yet and matched nothing */
            if (is_pk)
                include_new = true;
            if (keyless)
                include_old = true;
            if (old_var != NULL && new_var != NULL)
            {
                /* Compare values - simplified comparison */
                bool changed = (old_var->kind != new_var->kind);
                if (!changed && old_var->kind == VAR_TYPE_INT)
                    changed = (old_var->value.int_val != new_var->value.int_val);
                /* float, bool and uint were missing here, so `set flag = false`
                 * or a new float sent no column and replicated as a no-op.
                 * The float compares by bits: -0.0 vs 0.0 and NaN are changes. */
                else if (!changed && old_var->kind == VAR_TYPE_FLOAT)
                    changed = (memcmp(&old_var->value.float_val, &new_var->value.float_val,
                                      sizeof(double)) != 0);
                else if (!changed && old_var->kind == VAR_TYPE_BOOL)
                    changed = (old_var->value.bool_val != new_var->value.bool_val);
                else if (!changed && old_var->kind == VAR_TYPE_UINT)
                    changed = (old_var->value.uint_val != new_var->value.uint_val);
                else if (!changed && old_var->kind == VAR_TYPE_UNIVERSAL)
                    changed = (strcmp(old_var->value.str_val ? old_var->value.str_val : "",
                                      new_var->value.str_val ? new_var->value.str_val : "") != 0);

                if (changed)
                {
                    include_old = true;
                    include_new = true;
                }
            }
        }

        /*
         * old_var and new_var can alias for INSERT (new_tuple is always NULL
         * there, so the include_new block below falls back to old_var as the
         * row's value). Decide what each pointer is used for first (old_claimed /
         * new_claimed), then free only what neither claimed it.
         */
        {
            bool old_claimed = false;
            bool new_claimed = false;

            if (include_old && old_var != NULL)
            {
                entity.old_data[entity.old_data_count].key = attnum;
                entity.old_data[entity.old_data_count].value = old_var;
                entity.old_data_count++;
                old_claimed = true;
            }

            if (include_new)
            {
                VarType *val = (new_var != NULL) ? new_var : old_var;
                if (val != NULL)
                {
                    entity.new_data[entity.new_data_count].key = attnum;
                    entity.new_data[entity.new_data_count].value = val;
                    entity.new_data_count++;
                    if (val == new_var)
                        new_claimed = true;
                    else
                        old_claimed = true;
                }
            }

            if (old_var != NULL && !old_claimed)
                pfree(old_var);
            if (new_var != NULL && new_var != old_var && !new_claimed)
                pfree(new_var);
        }
    }

    /* before the call: one that fails may have reached some of the members */
    write_subid_before = g_write_subid;
    g_write_subid = GetCurrentSubTransactionId();

    /* Make gRPC call */
    memset(&reply, 0, sizeof(reply));
    ret = yt_cluster_grpc_modify(&entity, &reply);

    /* Cleanup entity */
    yt_cluster_pb_free_db_input_entity(&entity);

    g_in_trigger = false;

    if (ret != 0)
    {
        const char *msg;
        const char *kb;
        int         code;

        CHECK_FOR_INTERRUPTS();     /* a cancel ended the call: raise the cancel */

        pthread_mutex_lock(&g_yt_cluster_config.mutex);
        g_yt_cluster_config.connected = false;
        pthread_mutex_unlock(&g_yt_cluster_config.mutex);

        /* ytserv's code, when it sent one; 08006 otherwise */
        code = yt_cluster_error_sqlstate(yt_cluster_grpc_last_error(), &msg);
        /* no answer from ytserv at all: the row was not taken, and this
         * statement's failure means the transaction cannot commit - 57P03, as
         * ytserv's own "cannot take the write now", not 08006, an outcome unknown */
        if (code == ERRCODE_CONNECTION_FAILURE && strncmp(msg, "curl error: ", 12) == 0)
            code = ERRCODE_CANNOT_CONNECT_NOW;
        kb = yt_cluster_error_kb(msg);
        ereport(ERROR,
                (errcode(code),
                 errmsg("yt_synchronize: %s", msg),
                 kb ? errhint("%s", kb) : 0));
    }

    /* Mark the transaction so yt_cluster_xact_callback() completes it
     * at COMMIT - unless the server told us it registered no state for this tx,
     * in which case both the PREPARE and the COMMIT round trips would reach
     * server's completion step, which then does nothing.
     *
     * OR-in, never clear: a transaction can touch several tables and the flag is
     * per-transaction, so one statement that needs no completion must not undo an
     * earlier statement that does. Only the xact callback clears it. */
    if (!reply.skip_complete)
        g_txn_dirty = true;
    else
        g_write_subid = write_subid_before;     /* it reached no node */

    yt_cluster_pb_free_modify_reply(&reply);

    /* pace the client so it can't outrun the cluster consumer */
    if (g_sync_delay_ms > 0)
        pg_usleep((long) g_sync_delay_ms * 1000L);

    return PointerGetDatum(rettuple);
}

/*
 * yt_complete - commit/rollback notification
 */
Datum
yt_complete(PG_FUNCTION_ARGS)
{
    bool do_commit = PG_GETARG_BOOL(0);
    ClusterComplete req;
    CompleteReply reply;
    FullTransactionId txid;
    int ret;

    if (yt_cluster_is_endless_loop())
        PG_RETURN_INT32(0);

    /*
     * Not connected: nothing of this transaction reached ytserv when nothing is to complete
     * (g_txn_dirty) - it changed nothing, or only tables with no trigger, in a session that
     * has not connected yet. That completes as nothing, 0, as a transaction with no id below
     * does, and as a lite node answered (2026-11-14: a cluster node raised "not connected"
     * for it, the first transaction of a session only). Rows sent and the connection marked
     * down since: the error it was. A write a handler caught, rolled back over: refused, as
     * at the commit.
     */
    if (!g_yt_cluster_config.connected)
    {
        if (!g_txn_dirty)
        {
            if (do_commit)
                yt_cluster_refuse_undone();
            PG_RETURN_INT32(0);
        }
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("yt_complete: not connected")));
    }

    /* the top transaction's, as the trigger sends it: called in a subtransaction too */
    txid = GetTopFullTransactionIdIfAny();
    if (!FullTransactionIdIsValid(txid))
        PG_RETURN_INT32(0);

    /* the cluster commits this transaction's rows before its own
     * COMMIT: see yt_cluster_wal_durable() */
    if (do_commit)
    {
        yt_cluster_refuse_undone();
        yt_cluster_wal_durable();
    }

    memset(&req, 0, sizeof(req));
    req.txid = U64FromFullTransactionId(txid);
    req.commit = do_commit ? COMMIT_COMMIT : COMMIT_ROLLBACK;

    memset(&reply, 0, sizeof(reply));
    ret = yt_cluster_grpc_complete(&req, &reply);

    if (ret != 0)
    {
        const char *msg;
        const char *kb;
        int         code;

        CHECK_FOR_INTERRUPTS();     /* a cancel ended the call: raise the cancel */
        code = yt_cluster_error_sqlstate(yt_cluster_grpc_last_error(), &msg);
        kb = yt_cluster_error_kb(msg);
        ereport(ERROR,
                (errcode(code),
                 errmsg("yt_complete: %s", msg),
                 kb ? errhint("%s", kb) : 0));
    }

    PG_RETURN_INT32((int32)reply.result);
}

/*
 * yt_info - get cluster info
 */
Datum
yt_info(PG_FUNCTION_ARGS)
{
    text *input_text = PG_GETARG_TEXT_PP(0);
    char *input = text_to_cstring(input_text);
    InfoRequest req;
    InfoResponse resp;
    char *result;
    int ret;

    /* A blob's handover is the trigger's alone: from here anyone who may log in could
     * name any object, and overwrite or delete it with this cluster's keys. */
    if (strncmp(input, "s3+:", 4) == 0)
        ereport(ERROR, (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE), errmsg("yt_info: s3+: is not for SQL")));

    /* 'T' = Trigger module: return local version + description without server call.
     * Accepts either case. */
    if (input[0] != '\0' && input[1] == '\0' &&
        (input[0] == 'T' || input[0] == 't'))
    {
        StringInfoData json;
        initStringInfo(&json);
        appendStringInfo(&json,
            "[{\"name\":\"version\",\"value\":\"%s\",\"module\":\"T\"},"
            "{\"name\":\"description\",\"value\":\"%s\",\"module\":\"T\"}]",
            YT_CLUSTER_VERSION, YT_CLUSTER_DESCRIPTION);
        pfree(input);
        PG_RETURN_TEXT_P(cstring_to_text(json.data));
    }

    ensure_connected("yt_info");

    memset(&req, 0, sizeof(req));
    req.input = input;

    memset(&resp, 0, sizeof(resp));
    ret = yt_cluster_grpc_info(&req, &resp);

    pfree(req.input);

    if (ret != 0)
    {
        CHECK_FOR_INTERRUPTS();     /* a cancel ended the call: raise the cancel */
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("yt_info: %s", yt_cluster_grpc_last_error())));
    }

    result = yt_cluster_info_response_to_json(&resp);
    yt_cluster_pb_free_info_response(&resp);

    PG_RETURN_TEXT_P(cstring_to_text(result));
}

/*
 * yt_info_def - get cluster info (no args)
 */
Datum
yt_info_def(PG_FUNCTION_ARGS)
{
    InfoRequest req;
    InfoResponse resp;
    char *result;
    int ret;

    (void) fcinfo; /* suppress unused parameter warning */

    ensure_connected("yt_info");

    memset(&req, 0, sizeof(req));
    req.input = "";

    memset(&resp, 0, sizeof(resp));
    ret = yt_cluster_grpc_info(&req, &resp);

    if (ret != 0)
    {
        CHECK_FOR_INTERRUPTS();     /* a cancel ended the call: raise the cancel */
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("yt_info: %s", yt_cluster_grpc_last_error())));
    }

    result = yt_cluster_info_response_to_json(&resp);
    yt_cluster_pb_free_info_response(&resp);

    PG_RETURN_TEXT_P(cstring_to_text(result));
}

/*
 * yt_info_data - get info for term/subterm
 */
Datum
yt_info_data(PG_FUNCTION_ARGS)
{
    int64 term = PG_GETARG_INT64(0);
    int64 subterm = PG_GETARG_INT64(1);
    MergeRequest req;
    InfoResponse resp;
    EntityKey key;
    char *result;
    int ret;

    ensure_connected("yt_info");

    memset(&key, 0, sizeof(key));
    key.term = (uint64_t)term;
    key.subterm = (uint32_t)subterm;
    key.version = 0;

    memset(&req, 0, sizeof(req));
    req.key = &key;
    req.pull = false;
    req.force_commit = false;

    memset(&resp, 0, sizeof(resp));
    ret = yt_cluster_grpc_merge(&req, &resp);

    if (ret != 0)
    {
        CHECK_FOR_INTERRUPTS();     /* a cancel ended the call: raise the cancel */
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("yt_info: %s", yt_cluster_grpc_last_error())));
    }

    result = yt_cluster_info_response_to_json(&resp);
    yt_cluster_pb_free_info_response(&resp);

    PG_RETURN_TEXT_P(cstring_to_text(result));
}

/*
 * yt_info_data_version - get info for term/subterm/version
 */
Datum
yt_info_data_version(PG_FUNCTION_ARGS)
{
    int64 term = PG_GETARG_INT64(0);
    int64 subterm = PG_GETARG_INT64(1);
    int64 version = PG_GETARG_INT64(2);
    MergeRequest req;
    InfoResponse resp;
    EntityKey key;
    char *result;
    int ret;

    ensure_connected("yt_info");

    memset(&key, 0, sizeof(key));
    key.term = (uint64_t)term;
    key.subterm = (uint32_t)subterm;
    key.version = (uint32_t)version;

    memset(&req, 0, sizeof(req));
    req.key = &key;
    req.pull = false;
    req.force_commit = false;

    memset(&resp, 0, sizeof(resp));
    ret = yt_cluster_grpc_merge(&req, &resp);

    if (ret != 0)
    {
        CHECK_FOR_INTERRUPTS();     /* a cancel ended the call: raise the cancel */
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("yt_info: %s", yt_cluster_grpc_last_error())));
    }

    result = yt_cluster_info_response_to_json(&resp);
    yt_cluster_pb_free_info_response(&resp);

    PG_RETURN_TEXT_P(cstring_to_text(result));
}

/*
 * yt_merge - pull data from cluster
 */
Datum
yt_merge(PG_FUNCTION_ARGS)
{
    int64 term = PG_GETARG_INT64(0);
    int64 subterm = PG_GETARG_INT64(1);
    MergeRequest req;
    InfoResponse resp;
    EntityKey key;
    char *result;
    int ret;

    ensure_connected("yt_merge");

    elog(INFO, "pull data from cluster with info...");

    memset(&key, 0, sizeof(key));
    key.term = (uint64_t)term;
    key.subterm = (uint32_t)subterm;
    key.version = 0;

    memset(&req, 0, sizeof(req));
    req.key = &key;
    req.pull = true;
    req.force_commit = false;

    memset(&resp, 0, sizeof(resp));
    ret = yt_cluster_grpc_merge(&req, &resp);

    if (ret != 0)
    {
        CHECK_FOR_INTERRUPTS();     /* a cancel ended the call: raise the cancel */
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("yt_merge: %s", yt_cluster_grpc_last_error())));
    }

    result = yt_cluster_info_response_to_json(&resp);
    yt_cluster_pb_free_info_response(&resp);

    PG_RETURN_TEXT_P(cstring_to_text(result));
}

/*
 * yt_merge_force - pull data with force commit option
 */
Datum
yt_merge_force(PG_FUNCTION_ARGS)
{
    int64 term = PG_GETARG_INT64(0);
    int64 subterm = PG_GETARG_INT64(1);
    bool force_commit = PG_GETARG_BOOL(2);
    MergeRequest req;
    InfoResponse resp;
    EntityKey key;
    char *result;
    int ret;

    ensure_connected("yt_merge");

    if (force_commit)
        elog(INFO, "pull data from cluster, with info and force status update...");
    else
        elog(INFO, "pull data from cluster, with info...");

    memset(&key, 0, sizeof(key));
    key.term = (uint64_t)term;
    key.subterm = (uint32_t)subterm;
    key.version = 0;

    memset(&req, 0, sizeof(req));
    req.key = &key;
    req.pull = true;
    req.force_commit = force_commit;

    memset(&resp, 0, sizeof(resp));
    ret = yt_cluster_grpc_merge(&req, &resp);

    if (ret != 0)
    {
        CHECK_FOR_INTERRUPTS();     /* a cancel ended the call: raise the cancel */
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("yt_merge: %s", yt_cluster_grpc_last_error())));
    }

    result = yt_cluster_info_response_to_json(&resp);
    yt_cluster_pb_free_info_response(&resp);

    PG_RETURN_TEXT_P(cstring_to_text(result));
}

/*
 * yt_merge_def - pull data from cluster (no args)
 */
Datum
yt_merge_def(PG_FUNCTION_ARGS)
{
    MergeRequest req;
    InfoResponse resp;
    char *result;
    int ret;

    (void) fcinfo; /* suppress unused parameter warning */

    ensure_connected("yt_merge");

    elog(INFO, "pull data from cluster...");

    memset(&req, 0, sizeof(req));
    req.key = NULL;
    req.pull = true;
    req.force_commit = false;

    memset(&resp, 0, sizeof(resp));
    ret = yt_cluster_grpc_merge(&req, &resp);

    if (ret != 0)
    {
        CHECK_FOR_INTERRUPTS();     /* a cancel ended the call: raise the cancel */
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("yt_merge: %s", yt_cluster_grpc_last_error())));
    }

    result = yt_cluster_info_response_to_json(&resp);
    yt_cluster_pb_free_info_response(&resp);

    PG_RETURN_TEXT_P(cstring_to_text(result));
}

/*
 * The table-config codec is ytserv's (ConfigValue's JSON and base64
 * protobuf forms): it answers yt_info('<prefix><input>') with one row, the
 * converted value. A bad input comes back as its error.
 */
static text *
yt_ct_call(const char *func_name, const char *prefix, const char *input)
{
    InfoRequest req;
    InfoResponse resp;
    text *result;
    int ret;

    ensure_connected(func_name);

    memset(&req, 0, sizeof(req));
    req.input = psprintf("%s%s", prefix, input);

    memset(&resp, 0, sizeof(resp));
    ret = yt_cluster_grpc_info(&req, &resp);

    pfree(req.input);

    if (ret != 0)
    {
        CHECK_FOR_INTERRUPTS();     /* a cancel ended the call: raise the cancel */
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("%s: %s", func_name, yt_cluster_grpc_last_error())));
    }
    if (resp.data_count < 1 || resp.data[0].value == NULL)
    {
        yt_cluster_pb_free_info_response(&resp);
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("%s: ytserv answered no value", func_name)));
    }

    result = cstring_to_text(resp.data[0].value);
    yt_cluster_pb_free_info_response(&resp);
    return result;
}

/*
 * yt_ct_encode - a table config's JSON to the base64 protobuf of an A row
 */
Datum
yt_ct_encode(PG_FUNCTION_ARGS)
{
    char *input;
    text *result;

    if (PG_ARGISNULL(0))
        PG_RETURN_NULL();
    input = text_to_cstring(PG_GETARG_TEXT_PP(0));
    result = yt_ct_call("yt_ct_encode", "ct+:", input);
    pfree(input);
    PG_RETURN_TEXT_P(result);
}

/*
 * yt_ct_decode - an A row's base64 protobuf to its JSON; NULL or empty is the
 * table with every default
 */
Datum
yt_ct_decode(PG_FUNCTION_ARGS)
{
    char *input;
    text *result;

    input = PG_ARGISNULL(0) ? pstrdup("") : text_to_cstring(PG_GETARG_TEXT_PP(0));
    result = yt_ct_call("yt_ct_decode", "ct-:", input);
    pfree(input);
    PG_RETURN_TEXT_P(result);
}

/* ============================================================================
 * A blob, out of the row. The sync trigger is an AFTER trigger, too late to keep
 * it out of the row and the WAL: this one writes it to a file, names it to ytserv, and leaves
 * the column NULL, or a single zero byte if it is NOT NULL.
 * ============================================================================ */

/* The file a blob waits in. Readable by this user alone: it is plain text there. */
static char *
s3_write_file(const char *dir, const char *data, Size len, uint64 xid)
{
    static uint32 seq = 0;
    char   *root = psprintf("%s/yt-s3", dir);
    char   *path = psprintf("%s/%d-" UINT64_FORMAT "-%u.blob", root, MyProcPid, xid, ++seq);
    struct stat st;
    int     fd;
    Size    left = len;

    if (lstat(root, &st) != 0)
    {
        if (errno != ENOENT || (mkdir(root, 0700) != 0 && errno != EEXIST) || lstat(root, &st) != 0)
            ereport(ERROR, (errcode_for_file_access(), errmsg("yt_s3_blob: could not create %s: %m", root)));
    }
    /* under a shared directory: one another user made first could swap what is sent */
    if (!S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077) != 0)
        ereport(ERROR, (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
                        errmsg("yt_s3_blob: %s is not this user's own directory", root)));
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
        ereport(ERROR, (errcode_for_file_access(), errmsg("yt_s3_blob: could not create %s: %m", path)));
    while (left > 0)
    {
        ssize_t n = write(fd, data + (len - left), left);

        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
        {
            int e = errno;

            close(fd);
            unlink(path);
            errno = e;
            ereport(ERROR, (errcode_for_file_access(), errmsg("yt_s3_blob: could not write %s: %m", path)));
        }
        left -= (Size) n;
    }
    close(fd);
    pfree(root);
    return path;
}

/* Is the new value the old one? An UPDATE that leaves the blob alone sends nothing. */
static bool
s3_same_value(HeapTuple oldtup, HeapTuple newtup, int attnum, TupleDesc tupdesc)
{
    bool    onull, nnull;
    Datum   o = heap_getattr(oldtup, attnum, tupdesc, &onull);
    Datum   n = heap_getattr(newtup, attnum, tupdesc, &nnull);
    bytea  *a, *b;

    if (onull || nnull)
        return onull && nnull;
    a = DatumGetByteaPP(o);
    b = DatumGetByteaPP(n);
    return VARSIZE_ANY_EXHDR(a) == VARSIZE_ANY_EXHDR(b)
        && memcmp(VARDATA_ANY(a), VARDATA_ANY(b), VARSIZE_ANY_EXHDR(a)) == 0;
}

/*
 * An INSERT of a key already there. The trigger runs ahead of the unique check: sending the
 * blob would replace the committed row's object for a write that is about to fail, or that
 * ON CONFLICT DO NOTHING will drop.
 *
 * The primary key's own index and equality procs, not SQL: this runs for anyone who may insert,
 * and a query would run whatever `=` that session's search_path finds. As the unique check does,
 * it sees through a dirty snapshot, and waits for a transaction still deciding: a snapshot of
 * this statement would call a key taken that another committed the delete of.
 */
static bool
s3_key_taken(Relation rel, TupleDesc tupdesc, HeapTuple tuple, Oid pk_index, ItemPointer self)
{
    Relation    idx = index_open(pk_index, AccessShareLock);
    int         nkeys = IndexRelationGetNumberOfKeyAttributes(idx);
    ScanKeyData keys[INDEX_MAX_KEYS];
    bool        taken = false;
    int         i;

    for (i = 0; i < nkeys; i++)
    {
        AttrNumber att = idx->rd_index->indkey.values[i];
        bool    isnull;
        Datum   v = heap_getattr(tuple, att, tupdesc, &isnull);
        Oid     op;

        if (isnull)
        {
            index_close(idx, AccessShareLock);
            return false;       /* the insert fails on its own */
        }
        op = get_opfamily_member(idx->rd_opfamily[i], idx->rd_opcintype[i], idx->rd_opcintype[i], BTEqualStrategyNumber);
        if (!OidIsValid(op))
            elog(ERROR, "yt_s3_blob: no equality for key column %d", (int) att);
        ScanKeyEntryInitialize(&keys[i], 0, att, BTEqualStrategyNumber, idx->rd_opcintype[i],
                               idx->rd_indcollation[i], get_opcode(op), v);
    }
    index_close(idx, AccessShareLock);

    for (;;)
    {
        SnapshotData dirty;
        SysScanDesc scan;
        HeapTuple   found;
        TransactionId xwait = InvalidTransactionId;
        ScanKeyData scan_keys[INDEX_MAX_KEYS];

        /* up to PG 17 the scan writes index column numbers over the caller's: each its own copy */
        memcpy(scan_keys, keys, nkeys * sizeof(ScanKeyData));
        InitDirtySnapshot(dirty);
        scan = systable_beginscan(rel, pk_index, true, &dirty, nkeys, scan_keys);
        taken = false;
        while (HeapTupleIsValid(found = systable_getnext(scan)))
        {
            /* an UPDATE's own row, its key equal though its bytes are not */
            if (self != NULL && ItemPointerEquals(&found->t_self, self))
                continue;
            taken = true;
            xwait = TransactionIdIsValid(dirty.xmin) ? dirty.xmin : dirty.xmax;
            break;
        }
        systable_endscan(scan);
        if (!TransactionIdIsValid(xwait))
            return taken;
        XactLockTableWait(xwait, NULL, NULL, XLTW_None);
    }
}

/* An UPDATE that moves its row to another key */
static bool
s3_key_changed(TupleDesc tupdesc, Bitmapset *pk_attrs, HeapTuple before, HeapTuple after)
{
    int a;

    for (a = 1; a <= tupdesc->natts; a++)
    {
        Form_pg_attribute p = TupleDescAttr(tupdesc, a - 1);
        bool    n1;
        bool    n2;
        Datum   v1;
        Datum   v2;

        if (p->attisdropped || !bms_is_member(a - FirstLowInvalidHeapAttributeNumber, pk_attrs))
            continue;
        v1 = heap_getattr(before, a, tupdesc, &n1);
        v2 = heap_getattr(after, a, tupdesc, &n2);
        if (n1 != n2 || (!n1 && !datumIsEqual(v1, v2, p->attbyval, p->attlen)))
            return true;
    }
    return false;
}

/* The text of a key's value, the same from any session: an object's name is made of it */
static char *
s3_key_text(Oid typoid, Datum datum)
{
    pg_tz  *zone = session_timezone;
    char   *s = NULL;

    session_timezone = pg_tzset("UTC");
    PG_TRY();
    {
        s = output_canonical(typoid, datum);
    }
    PG_FINALLY();
    {
        session_timezone = zone;
    }
    PG_END_TRY();
    return s;
}

static void
s3_json_array_start(StringInfo buf, const char *name)
{
    appendStringInfo(buf, ",\"%s\":[", name);
}

Datum
yt_s3_blob(PG_FUNCTION_ARGS)
{
    TriggerData *td = (TriggerData *) fcinfo->context;
    Trigger    *tg;
    HeapTuple   tuple;
    TupleDesc   tupdesc;
    Relation    rel;
    Bitmapset  *pk_attrs;
    Oid         pk_constraint = InvalidOid;
    const char *skip;
    char      **args;
    Datum      *values;
    bool       *nulls;
    bool       *replace;
    bool        any = false;
    int         natts;
    int         i;

    if (!CALLED_AS_TRIGGER(fcinfo))
        elog(ERROR, "yt_s3_blob: not called as trigger");
    if (!TRIGGER_FIRED_BEFORE(td->tg_event) || !TRIGGER_FIRED_FOR_ROW(td->tg_event))
        elog(ERROR, "yt_s3_blob: a BEFORE row trigger");
    tuple = TRIGGER_FIRED_BY_UPDATE(td->tg_event) ? td->tg_newtuple : td->tg_trigtuple;
    /* a member applying the row has the marker already; ytserv storing it asks to be let by */
    skip = GetConfigOption("yt.s3_skip", true, false);
    if (yt_cluster_is_endless_loop() || (skip != NULL && strcmp(skip, "on") == 0))
        return PointerGetDatum(tuple);

    tg = td->tg_trigger;
    if (tg->tgnargs < 3 || tg->tgnargs % 2 != 1)
        elog(ERROR, "yt_s3_blob: its arguments are a directory, then each column and its mode");
    args = tg->tgargs;
    rel = td->tg_relation;
    tupdesc = RelationGetDescr(rel);
    natts = tupdesc->natts;
    pk_attrs = get_primary_key_attnos(RelationGetRelid(rel), false, &pk_constraint);
    if (bms_is_empty(pk_attrs))
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                        errmsg("yt_s3_blob: %s has no primary key: an object's key is made of it", RelationGetRelationName(rel))));
    /*
     * An empty key would name one object for every row. A key column is NULL here if it is generated,
     * which comes after this trigger, or set by a later one.
     */
    for (i = 1; i <= tupdesc->natts; i++)
    {
        if (!TupleDescAttr(tupdesc, i - 1)->attisdropped && bms_is_member(i - FirstLowInvalidHeapAttributeNumber, pk_attrs) && heap_attisnull(tuple, i, tupdesc))
            ereport(ERROR, (errcode(ERRCODE_NOT_NULL_VIOLATION),
                            errmsg("yt_s3_blob: key column %s of %s has no value yet: an object's key is made of it",
                                   NameStr(TupleDescAttr(tupdesc, i - 1)->attname), RelationGetRelationName(rel))));
    }
    /* sent nothing, kept as it is: the unique check, or ON CONFLICT, says what comes of it */
    if (TRIGGER_FIRED_BY_INSERT(td->tg_event))
    {
        if (s3_key_taken(rel, tupdesc, tuple, get_constraint_index(pk_constraint), NULL))
            return PointerGetDatum(tuple);
    }
    else if (s3_key_changed(tupdesc, pk_attrs, td->tg_trigtuple, tuple)
             && s3_key_taken(rel, tupdesc, tuple, get_constraint_index(pk_constraint), &td->tg_trigtuple->t_self))
        return PointerGetDatum(tuple);
    values = palloc0(natts * sizeof(Datum));
    nulls = palloc0(natts * sizeof(bool));
    replace = palloc0(natts * sizeof(bool));

    for (i = 1; i < tg->tgnargs; i += 2)
    {
        const char *column = args[i];
        const char *mode = args[i + 1];
        int         attnum = SPI_fnumber(tupdesc, column);
        Form_pg_attribute attr;
        Datum       value;
        bool        isnull;
        bool        first;
        char * volatile path = NULL;
        Size        size = 0;
        StringInfoData json;
        int         a;
        uint64      xid;

        if (attnum <= 0)
            ereport(ERROR, (errcode(ERRCODE_UNDEFINED_COLUMN), errmsg("yt_s3_blob: no column %s", column)));
        attr = TupleDescAttr(tupdesc, attnum - 1);
        if (attr->atttypid != BYTEAOID)
            ereport(ERROR, (errcode(ERRCODE_DATATYPE_MISMATCH), errmsg("yt_s3_blob: %s is not bytea", column)));
        if (TRIGGER_FIRED_BY_UPDATE(td->tg_event) && s3_same_value(td->tg_trigtuple, tuple, attnum, tupdesc))
            continue;
        value = heap_getattr(tuple, attnum, tupdesc, &isnull);
        /* NOT NULL holds for what the application wrote: the marker is no way around it */
        if (isnull && attr->attnotnull)
            continue;
        /* the marker itself, as a restore writes it back: the object is the value, and stays */
        if (!isnull && attr->attnotnull)
        {
            bytea *m = DatumGetByteaPP(value);

            if (VARSIZE_ANY_EXHDR(m) == 1 && *VARDATA_ANY(m) == 0)
                continue;
        }

        /* this subtransaction's: a savepoint rolled back is rolled back for the blob too */
        xid = U64FromFullTransactionId(GetCurrentFullTransactionId());
        if (!isnull)
        {
            bytea *b = DatumGetByteaPP(value);

            size = VARSIZE_ANY_EXHDR(b);
            path = s3_write_file(args[0], VARDATA_ANY(b), size, xid);
        }

        initStringInfo(&json);
        appendStringInfoString(&json, "{\"db\":");
        escape_json(&json, get_database_name(MyDatabaseId));
        appendStringInfoString(&json, ",\"schema\":");
        escape_json(&json, get_namespace_name(RelationGetNamespace(rel)));
        appendStringInfoString(&json, ",\"table\":");
        escape_json(&json, RelationGetRelationName(rel));
        appendStringInfoString(&json, ",\"column\":");
        escape_json(&json, column);
        appendStringInfoString(&json, ",\"mode\":");
        escape_json(&json, mode);
        appendStringInfoString(&json, ",\"path\":");
        escape_json(&json, path != NULL ? path : "");
        appendStringInfo(&json, ",\"xid\":" UINT64_FORMAT ",\"size\":" UINT64_FORMAT, xid, (uint64) size);
        s3_json_array_start(&json, "pk");
        first = true;
        for (a = 1; a <= natts; a++)
        {
            Form_pg_attribute p = TupleDescAttr(tupdesc, a - 1);
            bool    pnull;
            Datum   pv;

            if (p->attisdropped || !bms_is_member(a - FirstLowInvalidHeapAttributeNumber, pk_attrs))
                continue;
            pv = heap_getattr(tuple, a, tupdesc, &pnull);
            if (!first)
                appendStringInfoChar(&json, ',');
            first = false;
            escape_json(&json, pnull ? "" : s3_key_text(p->atttypid, pv));
        }
        appendStringInfoChar(&json, ']');
        s3_json_array_start(&json, "pk_names");
        first = true;
        for (a = 1; a <= natts; a++)
        {
            Form_pg_attribute p = TupleDescAttr(tupdesc, a - 1);

            if (p->attisdropped || !bms_is_member(a - FirstLowInvalidHeapAttributeNumber, pk_attrs))
                continue;
            if (!first)
                appendStringInfoChar(&json, ',');
            first = false;
            escape_json(&json, NameStr(p->attname));
        }
        appendStringInfoString(&json, "]}");

        /* refused or cancelled: this statement fails, and its file goes with it */
        PG_TRY();
        {
            pfree(yt_ct_call("yt_s3_blob", "s3+:", json.data));
        }
        PG_CATCH();
        {
            if (path != NULL)
                unlink(path);
            PG_RE_THROW();
        }
        PG_END_TRY();
        pfree(json.data);

        replace[attnum - 1] = true;
        if (attr->attnotnull)
        {
            bytea *marker = (bytea *) palloc(VARHDRSZ + 1);

            SET_VARSIZE(marker, VARHDRSZ + 1);
            *VARDATA(marker) = 0;
            values[attnum - 1] = PointerGetDatum(marker);
        }
        else
            nulls[attnum - 1] = true;
        any = true;
    }
    if (any)
        tuple = heap_modify_tuple(tuple, tupdesc, values, nulls, replace);
    return PointerGetDatum(tuple);
}

/* A test file, as a blob's is written: ytserv reads it, to know it sees what a backend wrote */
Datum
yt_s3_probe(PG_FUNCTION_ARGS)
{
    char   *dir = text_to_cstring(PG_GETARG_TEXT_PP(0));
    char   *path = s3_write_file(dir, "yt-s3", 5, GetTopFullTransactionId().value);

    PG_RETURN_TEXT_P(cstring_to_text(path));
}
