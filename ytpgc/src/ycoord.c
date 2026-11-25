/* SPDX-License-Identifier: MIT - Copyright (c) 2026 DBinvent */
/*
 * ycoord.c - cluster-wide advisory locks and sequences
 *
 *   yt_advisory_lock(bigint), yt_try_advisory_lock(bigint), yt_advisory_unlock(bigint),
 *   yt_advisory_xact_lock(bigint), yt_nextval(regclass), yt_create_sequence(regclass, int)
 *
 * As PostgreSQL's pg_advisory_lock() and nextval(), across the cluster: the node's ytserv
 * holds them on its etcd, whose leader decides (ytserv state/ycoord.rs). Asked by
 * yt_info prefixes over the unix socket:
 *
 * - a lock is held by this backend - its pid and its start time - per database; locked
 *   again it is held once more, as PostgreSQL's are; a session's lock and a transaction's
 *   apart. A blocking lock is asked again each second or so, with a check for a cancel in
 *   between: query cancel and statement_timeout work, and a wait given up leaves the queue.
 * - a transaction's locks go at its end (commit, abort, prepare), a session's at the
 *   backend's exit; a backend that dies without saying so is found by ytserv in
 *   pg_stat_activity, and a PostgreSQL restart frees every lock of the node.
 * - yt_nextval(seq) takes the next number of the cluster sequence made for the local
 *   sequence seq by yt_create_sequence: the local one is its twin. With each number the
 *   twin's last value goes along: one past what the cluster gave (numbers the twin gave
 *   while the cluster was off) moves the cluster sequence above it first.
 *
 * The cluster off for the session (yt.node = 'local', yt_cluster_is_endless_loop): the
 * locks are PostgreSQL's own advisory locks, and yt_nextval is the twin's nextval().
 */

#include "yt_cluster.h"

/* [nightly 2026-11-22] the transaction callbacks and GetCurrentSubTransactionId: PG16's headers bring
 * it in by another include, PG14's do not */
#include "access/xact.h"
#include "catalog/pg_class.h"
#include "commands/dbcommands.h"
#include "miscadmin.h"
#include "storage/ipc.h"
#include "utils/acl.h"
#include "utils/fmgrprotos.h"
#include "utils/lsyscache.h"
#include "utils/timestamp.h"

PG_FUNCTION_INFO_V1(yt_advisory_lock);
PG_FUNCTION_INFO_V1(yt_try_advisory_lock);
PG_FUNCTION_INFO_V1(yt_advisory_unlock);
PG_FUNCTION_INFO_V1(yt_advisory_xact_lock);
PG_FUNCTION_INFO_V1(yt_nextval);
PG_FUNCTION_INFO_V1(yt_create_sequence);

/* a cluster lock asked in this transaction: its end tells ytserv */
static bool g_ycoord_xact = false;
/* [review 2026-11-21] the transaction's cluster locks held, by the subtransaction that took
 * each: one rolled back lets its own go, as PostgreSQL's are; a PREPARE with any is refused */
typedef struct { int64 key; SubTransactionId subid; } YcoordXactLock;
static YcoordXactLock *g_ycoord_xact_locks = NULL;
static int g_ycoord_xact_n = 0;
static int g_ycoord_xact_cap = 0;
/* how long a release or a cancel may take: they run with interrupts held, or at the end */
#define YCOORD_END_TIMEOUT_MS 3000
/* a transaction's end not told: told before this backend's next ask */
static bool g_ycoord_xact_untold = false;
/* any cluster lock asked in this session: its end tells ytserv */
static bool g_ycoord_session = false;
static bool g_ycoord_exit_set = false;
/* this backend's database, read once: no catalog read in a subtransaction's abort */
static char *g_ycoord_db = NULL;

/* this backend, as ytserv names it: "<pid>|<start>" */
static char *
ycoord_owner(void)
{
    return psprintf("%d|" INT64_FORMAT, MyProcPid, (int64) MyStartTimestamp);
}

/* one ask of ytserv that must end: within YCOORD_END_TIMEOUT_MS; never raises */
static bool
ycoord_ask_bounded(const char *prefix, const char *text)
{
    const char *err = NULL;
    char       *v;

    yt_cluster_grpc_call_timeout(YCOORD_END_TIMEOUT_MS);
    v = yt_cluster_ask(prefix, text, &err);
    yt_cluster_grpc_call_timeout(0);
    if (v == NULL)
        return false;
    pfree(v);
    return true;
}

/* a transaction's or the session's end, told; false when ytserv could not be asked */
static bool
ycoord_tell_end(bool session)
{
    return ycoord_ask_bounded("lk*:", psprintf("%s|%c", ycoord_owner(), session ? 's' : 'x'));
}

/* a transaction lock held, by the subtransaction that took it */
static void
ycoord_xact_lock_held(int64 key)
{
    if (g_ycoord_xact_n == g_ycoord_xact_cap)
    {
        int cap = g_ycoord_xact_cap == 0 ? 8 : g_ycoord_xact_cap * 2;

        g_ycoord_xact_locks = g_ycoord_xact_locks == NULL
            ? MemoryContextAlloc(TopMemoryContext, cap * sizeof(YcoordXactLock))
            : repalloc(g_ycoord_xact_locks, cap * sizeof(YcoordXactLock));
        g_ycoord_xact_cap = cap;
    }
    g_ycoord_xact_locks[g_ycoord_xact_n].key = key;
    g_ycoord_xact_locks[g_ycoord_xact_n].subid = GetCurrentSubTransactionId();
    g_ycoord_xact_n++;
}

/*
 * [review 2026-11-21] A subtransaction's end: committed, its locks are its parent's; rolled
 * back, each lock it took goes - as PostgreSQL lets go of an aborted subtransaction's locks.
 * Never raises: one not let go here goes at the transaction's end.
 */
static void
ycoord_subxact_callback(SubXactEvent event, SubTransactionId mySubid, SubTransactionId parentSubid, void *arg)
{
    int i;
    int kept = 0;

    (void) arg;
    if (g_ycoord_xact_n == 0)
        return;
    if (event == SUBXACT_EVENT_COMMIT_SUB)
    {
        for (i = 0; i < g_ycoord_xact_n; i++)
            if (g_ycoord_xact_locks[i].subid == mySubid)
                g_ycoord_xact_locks[i].subid = parentSubid;
        return;
    }
    if (event != SUBXACT_EVENT_ABORT_SUB)
        return;
    for (i = 0; i < g_ycoord_xact_n; i++)
    {
        if (g_ycoord_xact_locks[i].subid != mySubid)
        {
            g_ycoord_xact_locks[kept++] = g_ycoord_xact_locks[i];
            continue;
        }
        if (g_ycoord_db != NULL)
            (void) ycoord_ask_bounded("lk-:", psprintf(INT64_FORMAT "|%s|x|%s", g_ycoord_xact_locks[i].key,
                                      ycoord_owner(), g_ycoord_db));
    }
    g_ycoord_xact_n = kept;
}

/* the backend's exit: every lock of its session goes */
static void
ycoord_exit(int code, Datum arg)
{
    (void) code;
    (void) arg;
    if (g_ycoord_session)
        (void) ycoord_tell_end(true);
}

/* before the first ask of a backend: its exit told; a transaction's end not told yet, told */
static void
ycoord_before_ask(const char *fn)
{
    yt_cluster_ensure_connected(fn);
    if (g_ycoord_db == NULL)
    {
        char *db = get_database_name(MyDatabaseId);

        g_ycoord_db = MemoryContextStrdup(TopMemoryContext, db != NULL ? db : "");
    }
    if (!g_ycoord_exit_set)
    {
        before_shmem_exit(ycoord_exit, (Datum) 0);
        g_ycoord_exit_set = true;
    }
    /* only while this transaction has asked nothing yet: else its own locks would go */
    if (g_ycoord_xact_untold && !g_ycoord_xact && ycoord_tell_end(false))
        g_ycoord_xact_untold = false;
    g_ycoord_session = true;
    g_ycoord_xact = true;
}

/*
 * The transaction's end: its cluster locks go, and a wait an error left is taken out of
 * its queue. Never raises - not told, it is told before the backend's next ask, and its
 * exit or ytserv's sweep frees the rest.
 */
static void
ycoord_xact_callback(XactEvent event, void *arg)
{
    (void) arg;
    if (!g_ycoord_xact)
        return;
    /* [review 2026-11-21] PostgreSQL moves a transaction's advisory locks to the prepared
     * transaction; a cluster lock of the backend cannot follow it there: refused, before
     * anything is prepared */
    if (event == XACT_EVENT_PRE_PREPARE && g_ycoord_xact_n > 0)
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg("cannot PREPARE a transaction that holds cluster transaction locks (yt_advisory_xact_lock)")));
    if (event == XACT_EVENT_COMMIT || event == XACT_EVENT_ABORT || event == XACT_EVENT_PREPARE
        || event == XACT_EVENT_PARALLEL_COMMIT || event == XACT_EVENT_PARALLEL_ABORT)
    {
        g_ycoord_xact = false;
        g_ycoord_xact_n = 0;
        if (!ycoord_tell_end(false))
            g_ycoord_xact_untold = true;
    }
}

void
yt_ycoord_init(void)
{
    RegisterXactCallback(ycoord_xact_callback, NULL);
    RegisterSubXactCallback(ycoord_subxact_callback, NULL);
}

/* ytserv's answer, or an ERROR naming the function */
static char *
ycoord_ask(const char *fn, const char *prefix, const char *text)
{
    const char *err = NULL;
    char       *v = yt_cluster_ask(prefix, text, &err);

    if (v == NULL)
    {
        /* [ycoord] a cancel or a statement_timeout that came while the ask was on the way broke
         * the call off: that is the error to raise (57014), not the cluster's (08006) */
        CHECK_FOR_INTERRUPTS();
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("%s: the cluster could not be asked: %s", fn, err ? err : "no answer")));
    }
    return v;
}

/*
 * A cluster lock of `key` for this backend: true once held; a try answers at once, false
 * while another holds it or waits for it. A blocking one asks again until held, with a
 * check for a cancel each time; given up - a cancel, a timeout, any error - its place in
 * the queue is left.
 */
static bool
ycoord_lock(const char *fn, int64 key, char scope, bool try_only)
{
    char *db = get_database_name(MyDatabaseId);
    char *ask;
    char *cancel;
    bool  held = false;

    ycoord_before_ask(fn);
    ask = psprintf(INT64_FORMAT "|%s|%c|%c|%s", key, ycoord_owner(), scope, try_only ? 't' : 'w', db);
    cancel = psprintf(INT64_FORMAT "|%s|%s", key, ycoord_owner(), db);
    PG_TRY();
    {
        for (;;)
        {
            char *v;

            CHECK_FOR_INTERRUPTS();
            v = ycoord_ask(fn, "lk+:", ask);
            if (strcmp(v, "ok") == 0)
            {
                held = true;
                break;
            }
            if (strcmp(v, "busy") == 0)
                break;
            if (strcmp(v, "wait") != 0)
                ereport(ERROR,
                        (errcode(ERRCODE_INTERNAL_ERROR),
                         errmsg("%s: an answer that is none of ok, wait, busy: %s", fn, v)));
            pfree(v);
            /* ytserv waited its step already: ask again */
        }
    }
    PG_CATCH();
    {
        HOLD_INTERRUPTS();
        (void) ycoord_ask_bounded("lkc:", cancel);
        RESUME_INTERRUPTS();
        PG_RE_THROW();
    }
    PG_END_TRY();
    return held;
}

/* yt_advisory_lock(key bigint): waits until this backend holds the cluster lock, for the session */
Datum
yt_advisory_lock(PG_FUNCTION_ARGS)
{
    int64 key = PG_GETARG_INT64(0);

    if (yt_cluster_is_endless_loop())
        return DirectFunctionCall1(pg_advisory_lock_int8, Int64GetDatum(key));
    (void) ycoord_lock("yt_advisory_lock", key, 's', false);
    PG_RETURN_VOID();
}

/* yt_advisory_xact_lock(key bigint): as yt_advisory_lock, held until the transaction ends */
Datum
yt_advisory_xact_lock(PG_FUNCTION_ARGS)
{
    int64 key = PG_GETARG_INT64(0);

    if (yt_cluster_is_endless_loop())
        return DirectFunctionCall1(pg_advisory_xact_lock_int8, Int64GetDatum(key));
    (void) ycoord_lock("yt_advisory_xact_lock", key, 'x', false);
    ycoord_xact_lock_held(key);
    PG_RETURN_VOID();
}

/* yt_try_advisory_lock(key bigint) -> bool: the cluster lock now, or false */
Datum
yt_try_advisory_lock(PG_FUNCTION_ARGS)
{
    int64 key = PG_GETARG_INT64(0);

    if (yt_cluster_is_endless_loop())
        return DirectFunctionCall1(pg_try_advisory_lock_int8, Int64GetDatum(key));
    PG_RETURN_BOOL(ycoord_lock("yt_try_advisory_lock", key, 's', true));
}

/* yt_advisory_unlock(key bigint) -> bool: one hold of the session's lock let go; false,
 * with a warning as PostgreSQL's, when this backend held none */
Datum
yt_advisory_unlock(PG_FUNCTION_ARGS)
{
    int64 key = PG_GETARG_INT64(0);
    char *db;
    char *v;
    bool  held;

    if (yt_cluster_is_endless_loop())
        return DirectFunctionCall1(pg_advisory_unlock_int8, Int64GetDatum(key));
    db = get_database_name(MyDatabaseId);
    ycoord_before_ask("yt_advisory_unlock");
    v = ycoord_ask("yt_advisory_unlock", "lk-:", psprintf(INT64_FORMAT "|%s|s|%s", key, ycoord_owner(), db));
    held = strcmp(v, "t") == 0;
    if (!held)
        ereport(WARNING,
                (errmsg("yt_advisory_unlock: this session does not hold cluster lock " INT64_FORMAT, key)));
    PG_RETURN_BOOL(held);
}

/* the sequence `relid`, checked as nextval() checks it, as "<schema>.<name>" */
static char *
ycoord_sequence(const char *fn, Oid relid, AclMode want)
{
    char *name = get_rel_name(relid);

    if (name == NULL || get_rel_relkind(relid) != RELKIND_SEQUENCE)
        ereport(ERROR,
                (errcode(ERRCODE_WRONG_OBJECT_TYPE),
                 errmsg("%s: %u is not a sequence", fn, relid)));
    if (pg_class_aclcheck(relid, GetUserId(), want) != ACLCHECK_OK)
        ereport(ERROR,
                (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
                 errmsg("%s: permission denied for sequence %s", fn, name)));
    return psprintf("%s.%s", get_namespace_name(get_rel_namespace(relid)), name);
}

/* the twin's last value as text, or "" while it has given none */
static char *
ycoord_twin(Oid relid)
{
    LOCAL_FCINFO(fcinfo, 1);
    Datum d;

    InitFunctionCallInfoData(*fcinfo, NULL, 1, InvalidOid, NULL, NULL);
    fcinfo->args[0].value = ObjectIdGetDatum(relid);
    fcinfo->args[0].isnull = false;
    d = pg_sequence_last_value(fcinfo);
    if (fcinfo->isnull)
        return pstrdup("");
    return psprintf(INT64_FORMAT, DatumGetInt64(d));
}

static int64
ycoord_number(const char *fn, const char *v)
{
    char *end = NULL;
    long long n;

    errno = 0;
    n = strtoll(v, &end, 10);
    if (errno != 0 || end == v || (*end != '\0' && *end != ','))
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("%s: an answer that is no number: %s", fn, v)));
    return (int64) n;
}

/* yt_nextval(seq regclass) -> bigint: the cluster sequence's next number; the cluster off,
 * the twin's nextval() */
Datum
yt_nextval(PG_FUNCTION_ARGS)
{
    Oid   relid = PG_GETARG_OID(0);
    char *seq;
    char *db;
    char *v;

    if (yt_cluster_is_endless_loop())
        return DirectFunctionCall1(nextval_oid, ObjectIdGetDatum(relid));
    seq = ycoord_sequence("yt_nextval", relid, ACL_USAGE | ACL_UPDATE);
    yt_cluster_ensure_connected("yt_nextval");
    db = get_database_name(MyDatabaseId);
    /* [review 2026-11-21] the database's name by its length: it may hold a '|' */
    v = ycoord_ask("yt_nextval", "sq:", psprintf("%s|%zu|%s%s", ycoord_twin(relid), strlen(db), db, seq));
    PG_RETURN_INT64(ycoord_number("yt_nextval", v));
}

/* yt_create_sequence(seq regclass, cache int default 1, if_missing bool default false) -> bigint:
 * the cluster sequence of the local sequence seq, starting above its last value; each node
 * takes `cache` numbers at once. Its owner's, or a superuser's. The first number it hands
 * out. There already: refused - or with if_missing moved above this node's seq, its next. */
Datum
yt_create_sequence(PG_FUNCTION_ARGS)
{
    Oid   relid = PG_GETARG_OID(0);
    int32 cache = PG_ARGISNULL(1) ? 1 : PG_GETARG_INT32(1);
    bool  if_missing = PG_NARGS() > 2 && !PG_ARGISNULL(2) && PG_GETARG_BOOL(2);
    char *seq;
    char *db;
    char *v;

    if (yt_cluster_is_endless_loop())
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("yt_create_sequence: the cluster is off for this session (yt.node = 'local')")));
    if (cache < 1)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("yt_create_sequence: a cache of at least 1, not %d", cache)));
    seq = ycoord_sequence("yt_create_sequence", relid, ACL_USAGE);
#if PG_VERSION_NUM >= 160000
    if (!object_ownercheck(RelationRelationId, relid, GetUserId()))
#else
    if (!pg_class_ownercheck(relid, GetUserId()))
#endif
        ereport(ERROR,
                (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
                 errmsg("yt_create_sequence: must be the owner of sequence %s", seq)));
    yt_cluster_ensure_connected("yt_create_sequence");
    db = get_database_name(MyDatabaseId);
    v = ycoord_ask("yt_create_sequence", "sq+:", psprintf("%d|%c|%s|%zu|%s%s", cache, if_missing ? 'm' : 'c',
                   ycoord_twin(relid), strlen(db), db, seq));
    PG_RETURN_INT64(ycoord_number("yt_create_sequence", v));
}
