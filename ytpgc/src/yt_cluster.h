/* SPDX-License-Identifier: MIT - Copyright (c) 2026 DBinvent */
/*
 * yt_cluster.h - YT PostgreSQL Extension Header
 *
 * PostgreSQL extension for YT Cluster
 * C implementation
 */

#ifndef YT_CLUSTER_H
#define YT_CLUSTER_H

#include "postgres.h"
#include "fmgr.h"
#include "commands/trigger.h"
#include "executor/spi.h"
#include "utils/builtins.h"
#include "utils/rel.h"
#include "utils/guc.h"
#include "access/htup_details.h"
/*
 * [pg-minor] The minors of November 2026 (18.7 and those of 14-17, 19) have heap_getattr(), an
 * inline function, call heap_datum_check() for a tuple flagged as a versioned datum. A
 * server before them exports no such function and sets no such flag: a library built
 * against the new headers would not load there ("undefined symbol: heap_datum_check" -
 * PostgreSQL 18.6 does not start). A weak reference: it loads on those, where the call is
 * never reached, and calls the server's own on the newer ones.
 */
#pragma weak heap_datum_check
#include "catalog/pg_type.h"
#include "funcapi.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

/* Constants - YT_CLUSTER_VERSION and YT_CLUSTER_DESCRIPTION may be overridden by -D from the Makefile */
#ifndef YT_CLUSTER_VERSION
#define YT_CLUSTER_VERSION "1.21"
#endif
#ifndef YT_CLUSTER_DESCRIPTION
#define YT_CLUSTER_DESCRIPTION "YT+RPPD Cluster PostgreSQL Extension (C implementation)"
#endif
/* the time to connect to the node - its calls themselves have no time limit
 * (grpc_client.c cancel_check) */
#define YT_CLUSTER_CONNECT_TIMEOUT_MS 5000
#define YT_CLUSTER_NODE_GUC "yt.node"
#define YT_CLUSTER_LOCAL "local"
#define YT_CLUSTER_MAX_HOST_LEN 256
#define YT_CLUSTER_MAX_ERROR_LEN 512
#define YT_CLUSTER_MAX_COLUMNS 256

/* ============================================================================
 * Base Types (from base.proto)
 * ============================================================================ */

/* PeerUuid - u128 wrapper */
typedef struct {
    uint64_t hi;
    uint64_t lo;
} PeerUuid;

/* BaseApi */
typedef struct {
    int32_t version;
    uint32_t caller_idx;
    PeerUuid *peer;
} BaseApi;

/* ClusterCommitType enum */
typedef enum {
    COMMIT_PREPARE = 0,
    COMMIT_COMMIT = 1,
    COMMIT_ROLLBACK = 2
} ClusterCommitType;

/* VarType value type enum */
typedef enum {
    VAR_TYPE_NULL = 0,
    VAR_TYPE_INT = 1,
    VAR_TYPE_UINT = 2,
    VAR_TYPE_FLOAT = 3,
    VAR_TYPE_BOOL = 4,
    VAR_TYPE_UNIVERSAL = 5,
    VAR_TYPE_DIFF = 6,
    VAR_TYPE_PATCH = 7
} VarTypeKind;

/* PatchType enum */
typedef enum {
    PATCH_TEXT = 0,
    PATCH_JSON = 1,
    PATCH_TXT = 2,
    PATCH_XML = 3,
    PATCH_YAML = 4
} PatchType;

/* Patch structure */
typedef struct {
    PatchType diff_type;
    char **diff;
    int diff_count;
} Patch;

/* VarType - Native and string values */
typedef struct {
    VarTypeKind kind;
    union {
        int64_t int_val;
        uint64_t uint_val;
        double float_val;
        bool bool_val;
        char *str_val;      /* Universal or Diff */
        Patch *patch_val;
    } value;
} VarType;

/* YtColumnRef - Table column reference (prefixed to avoid PostgreSQL conflict) */
typedef struct {
    uint32_t attnum;
    uint32_t oid;
    char *name;
    bool pk;
    uint32_t atttypid;
    int32_t atttypmod;
    bool ignore;
} YtColumnRef;

/* TableRef - Table reference */
typedef struct {
    uint32_t oid;
    char *db;
    char *schema;
    char *table;
    YtColumnRef *columns;
    int columns_count;
} TableRef;

/* EntityKey */
typedef struct {
    uint64_t term;
    uint32_t subterm;
    uint32_t version;
} EntityKey;

/* DataVersion */
typedef struct {
    uint64_t term;
    uint32_t *subterm;
    int subterm_count;
    uint32_t last;
    uint32_t *except;
    int except_count;
} DataVersion;

/* ClusterComplete */
typedef struct {
    uint64_t txid;
    ClusterCommitType commit;
    EntityKey *key;
    PeerUuid *orig;
    EntityKey *prev;
    BaseApi *base;
} ClusterComplete;

/* ============================================================================
 * DB Types (from db.proto)
 * ============================================================================ */

/* DbAction enum */
typedef enum {
    DB_ACTION_UPDATE = 0,
    DB_ACTION_INSERT = 1,
    DB_ACTION_DELETE = 2,
    DB_ACTION_TRUNCATE = 3
} DbAction;

/* PgIso enum - PostgreSQL isolation levels */
typedef enum {
    PG_ISO_READ_COMMITTED = 0,
    PG_ISO_READ_UNCOMMITTED = 1,
    PG_ISO_REPEATABLE_READ = 2,
    PG_ISO_SERIALIZABLE = 3
} PgIso;

/* DataVersionDump */
typedef struct {
    uint32_t term_delta;
    uint32_t subterm;
} DataVersionDump;

/* DataVersionDumpTo */
typedef struct {
    char *table;
    uint64_t term_base;
    DataVersionDump *data_version_dump;
} DataVersionDumpTo;

/* KeyValue pair for maps */
typedef struct {
    uint32_t key;
    VarType *value;
} KeyValue;

/* DbInputEntity - cluster save request */
typedef struct {
    uint64_t tx;
    TableRef *table;
    KeyValue *old_data;
    int old_data_count;
    KeyValue *new_data;
    int new_data_count;
    DbAction action;
    PeerUuid *origin;
    PgIso iso;
    char *user_role;
    char *jwt;
} DbInputEntity;

/* ModifyReply */
typedef struct {
    uint64_t result;
    DataVersionDumpTo *dump;
    /* Server registered no state for this tx, so the complete()
     * call at COMMIT would be a no-op and can be skipped. False (the proto3
     * default, and what an older server sends) means complete as before. */
    bool skip_complete;
} ModifyReply;

/* CompleteReply */
typedef struct {
    uint64_t result;
} CompleteReply;

/* CfgVo - Config value object */
typedef struct {
    char *name;
    char *value;
    char *module;
} CfgVo;

/* InfoRequest */
typedef struct {
    char *input;
} InfoRequest;

/* InfoResponse */
typedef struct {
    CfgVo *data;
    int data_count;
} InfoResponse;

/* LlmRequest - yt_llm_query() */
typedef struct {
    char *question;
    char *db;
    char *role;
    char **schemas;
    int schemas_count;
    uint32_t page;
    uint32_t size;
} LlmRequest;

/* LlmReply */
typedef struct {
    char *type;
    char *sql;
    char *json;
} LlmReply;

/* MergeRequest */
typedef struct {
    EntityKey *key;
    bool pull;
    bool force_commit;
} MergeRequest;

/* ============================================================================
 * Cluster Types (from cluster.proto)
 * ============================================================================ */

/* ClusterCommitMerge */
typedef struct {
    bool all_fields;
    char **text_col;
    int text_col_count;
} ClusterCommitMerge;

/* CfgTabl - Configuration Table Object */
typedef struct {
    uint32_t timeout_sec;
    /* cluster_commit oneof */
    int cluster_commit_type;  /* 0=none, 2=half, 3=all, 4=nodes, 5=ignore */
    uint32_t cluster_commit_value;
    ClusterCommitMerge *merge;
    bool sequential_consistency;
    bool aggregate_timeout;
    bool on_commit_python;
    bool on_commit_etcd;
    bool on_commit_queue;
    bool shard;
    bool az;
    char **ignore_col;
    int ignore_col_count;
} CfgTabl;

/* ConfigValue */
typedef struct {
    CfgTabl *table;
} ConfigValue;

/* CheckinReply */
typedef struct {
    PeerUuid *parent;
    int parent_count;
    PeerUuid *party;
    int party_count;
    EntityKey *key;
    /* required TermSubTerm */
    uint32_t *required_term_back;
    uint32_t *required_subterm;
    int required_count;
} CheckinReply;

/* ============================================================================
 * Connection and Configuration
 * ============================================================================ */

/* Connection configuration */
typedef struct {
    char server_path[YT_CLUSTER_MAX_HOST_LEN];
    bool loaded;
    bool connected;
    char last_error[YT_CLUSTER_MAX_ERROR_LEN];
    pthread_mutex_t mutex;
} YtClusterConfig;

/* Global config */
extern YtClusterConfig g_yt_cluster_config;

/* ============================================================================
 * gRPC Client Functions (grpc_client.c)
 * ============================================================================ */

int yt_cluster_grpc_connect(const char *host);
void yt_cluster_grpc_disconnect(void);
int yt_cluster_grpc_modify(const DbInputEntity *req, ModifyReply *resp);
int yt_cluster_grpc_complete(const ClusterComplete *req, CompleteReply *resp);
int yt_cluster_grpc_info(const InfoRequest *req, InfoResponse *resp);
int yt_cluster_grpc_llm(const LlmRequest *req, LlmReply *resp);
int yt_cluster_grpc_merge(const MergeRequest *req, InfoResponse *resp);
const char *yt_cluster_grpc_last_error(void);

/* ============================================================================
 * Protobuf Functions (protobuf.c)
 * ============================================================================ */

/* Encoding functions */
size_t yt_cluster_pb_encode_db_input_entity(const DbInputEntity *req, uint8_t **out);
size_t yt_cluster_pb_encode_cluster_complete(const ClusterComplete *req, uint8_t **out);
size_t yt_cluster_pb_encode_info_request(const InfoRequest *req, uint8_t **out);
size_t yt_cluster_pb_encode_merge_request(const MergeRequest *req, uint8_t **out);
size_t yt_cluster_pb_encode_llm_request(const LlmRequest *req, uint8_t **out);

/* Decoding functions */
int yt_cluster_pb_decode_modify_reply(const uint8_t *data, size_t len, ModifyReply *resp);
int yt_cluster_pb_decode_complete_reply(const uint8_t *data, size_t len, CompleteReply *resp);
int yt_cluster_pb_decode_info_response(const uint8_t *data, size_t len, InfoResponse *resp);
int yt_cluster_pb_decode_llm_reply(const uint8_t *data, size_t len, LlmReply *resp);

/* Free functions */
void yt_cluster_pb_free_modify_reply(ModifyReply *resp);
void yt_cluster_pb_free_info_response(InfoResponse *resp);
void yt_cluster_pb_free_llm_reply(LlmReply *resp);
void yt_cluster_pb_free_db_input_entity(DbInputEntity *req);


/* ============================================================================
 * Utility Functions
 * ============================================================================ */

char *yt_cluster_get_guc_string(const char *name);
bool yt_cluster_is_endless_loop(void);
/* [ycoord] yt_info('<prefix><text>') of ytserv: its one-row answer (palloc'd), or NULL with the
 * reason in *err - never raises */
char *yt_cluster_ask(const char *prefix, const char *text, const char **err);
/* [ycoord] cluster-wide advisory locks and sequences (ycoord.c): its transaction callback */
void yt_ycoord_init(void);
/* [ycoord] a time limit on each call from now on, ms; 0 none */
void yt_cluster_grpc_call_timeout(long ms);
char *yt_cluster_info_response_to_json(const InfoResponse *resp);
void yt_cluster_ensure_connected(const char *func_name);

#endif /* YT_CLUSTER_H */
