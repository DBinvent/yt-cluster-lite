/* SPDX-License-Identifier: MIT - Copyright (c) 2026 DBinvent */
/*
 * grpc_client.c - gRPC Client Implementation using libcurl HTTP/2
 *
 * Implements gRPC protocol over HTTP/2 using libcurl for the YT Cluster extension
 * Reference: https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-WEB.md
 */

#include "yt_cluster.h"
#include "proto/db.pb-c.h"
#include "miscadmin.h"     /* QueryCancelPending, ProcDiePending, INTERRUPTS_CAN_BE_PROCESSED */

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strncasecmp */
#include <ctype.h>     /* isxdigit */
#include <stdint.h>
#include <time.h>      /* clock_gettime (vDSO, no syscall) */
#include <sys/socket.h>
#include <sys/un.h>

/* gRPC frame header size */
#define GRPC_HEADER_SIZE 5

/* Response buffer */
typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} ResponseBuffer;

/* Global state */
static CURL *g_curl = NULL;
static char g_host[YT_CLUSTER_MAX_HOST_LEN] = "";
static char g_last_error[YT_CLUSTER_MAX_ERROR_LEN] = "";
static pthread_mutex_t g_curl_mutex = PTHREAD_MUTEX_INITIALIZER;

/* A failed gRPC call returns HTTP 200 with NO DATA frame and
 * the real error in the HTTP/2 trailers `grpc-status` / `grpc-message`. Without
 * this the client fell through to parse_grpc_frame() and reported the generic
 * "Invalid gRPC frame: too short (0 bytes)", discarding grpc-message. These
 * capture the trailers (set under g_curl_mutex, which grpc_call() holds) so a
 * server-side aborted status (with its message) reaches g_last_error. */
static long g_grpc_status = -1;                 /* -1 = no trailer seen */
static char g_grpc_message[YT_CLUSTER_MAX_ERROR_LEN] = "";

/* Per-phase accounting for one gRPC round trip, to attribute the
 * ~0.55 ms/call the trigger costs. Enabled by setting YT_CLUSTER_PROF=<n> in the
 * postmaster environment: every <n> calls one LOG line reports the running
 * per-call averages, then the counters reset. Unset (or 0) = fully off, the
 * only residual cost being three vDSO clock_gettime() reads per call.
 *   setup   = curl_easy_reset + ~14 setopt + slist build
 *   perform = curl_easy_perform (HTTP/2 framing + the socket round trip)
 *   post    = grpc frame parse + response copy + frees
 * Caller-side encode/decode (protobuf-c) is timed separately in
 * yt_cluster_grpc_modify() / yt_cluster_grpc_complete() and folded in as `codec`. */
static long   g_prof_every = -1;                /* -1 = env not read yet */
static uint64 g_prof_calls = 0;
static uint64 g_prof_ns_setup = 0;
static uint64 g_prof_ns_perform = 0;
static uint64 g_prof_ns_post = 0;
static uint64 g_prof_ns_codec = 0;
static uint64 g_prof_ns_lock = 0;

static inline uint64
prof_now(void)
{
    struct timespec ts;

    if (g_prof_every == 0)
        return 0;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64) ts.tv_sec * 1000000000ULL + (uint64) ts.tv_nsec;
}

static void
prof_init(void)
{
    const char *e;

    if (g_prof_every >= 0)
        return;
    e = getenv("YT_CLUSTER_PROF");
    g_prof_every = (e != NULL) ? atol(e) : 0;
    if (g_prof_every < 0)
        g_prof_every = 0;
}

/* Called with g_curl_mutex held. */
static void
prof_report(void)
{
    uint64 n = g_prof_calls;

    if (g_prof_every <= 0 || n < (uint64) g_prof_every)
        return;

    elog(LOG, "[YT_PROF] %lu calls: total %.1f us/call = lock %.1f + setup %.1f "
              "+ perform %.1f + post %.1f + codec %.1f",
         (unsigned long) n,
         (g_prof_ns_lock + g_prof_ns_setup + g_prof_ns_perform +
          g_prof_ns_post + g_prof_ns_codec) / 1000.0 / n,
         g_prof_ns_lock / 1000.0 / n,
         g_prof_ns_setup / 1000.0 / n,
         g_prof_ns_perform / 1000.0 / n,
         g_prof_ns_post / 1000.0 / n,
         g_prof_ns_codec / 1000.0 / n);

    g_prof_calls = 0;
    g_prof_ns_setup = g_prof_ns_perform = g_prof_ns_post = 0;
    g_prof_ns_codec = g_prof_ns_lock = 0;
}

/* codec time is accumulated outside the mutex; grpc_call() folds it in. */
static uint64 g_prof_pending_codec = 0;

/* percent-decode grpc-message in place (RFC: grpc-message is %-encoded) */
static void
grpc_pct_decode(char *s)
{
    char *w = s;
    for (char *r = s; *r; r++)
    {
        if (r[0] == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2]))
        {
            char h[3] = { r[1], r[2], 0 };
            *w++ = (char)strtol(h, NULL, 16);
            r += 2;
        }
        else
            *w++ = *r;
    }
    *w = '\0';
}

/*
 * libcurl header callback - runs for response headers AND HTTP/2 trailers.
 */
static size_t
header_callback(char *buffer, size_t size, size_t nitems, void *userp)
{
    size_t len = size * nitems;
    static const char K_ST[] = "grpc-status:";
    static const char K_MS[] = "grpc-message:";
    (void) userp;

    if (len >= sizeof(K_ST) - 1 && strncasecmp(buffer, K_ST, sizeof(K_ST) - 1) == 0)
    {
        char v[32];
        size_t off = sizeof(K_ST) - 1, n = 0;
        while (off < len && (buffer[off] == ' ' || buffer[off] == '\t')) off++;
        while (off < len && n < sizeof(v) - 1 && buffer[off] != '\r' && buffer[off] != '\n')
            v[n++] = buffer[off++];
        v[n] = '\0';
        if (n > 0)
            g_grpc_status = strtol(v, NULL, 10);
    }
    else if (len >= sizeof(K_MS) - 1 && strncasecmp(buffer, K_MS, sizeof(K_MS) - 1) == 0)
    {
        size_t off = sizeof(K_MS) - 1, n = 0;
        while (off < len && (buffer[off] == ' ' || buffer[off] == '\t')) off++;
        while (off < len && n < sizeof(g_grpc_message) - 1 && buffer[off] != '\r' && buffer[off] != '\n')
            g_grpc_message[n++] = buffer[off++];
        g_grpc_message[n] = '\0';
        grpc_pct_decode(g_grpc_message);
    }
    return len;
}

/*
 * libcurl write callback
 */
static size_t
write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    ResponseBuffer *buf = (ResponseBuffer *)userp;

    if (buf->size + realsize > buf->capacity)
    {
        uint8_t *new_data;
        size_t new_capacity = buf->capacity * 2;
        if (new_capacity < buf->size + realsize)
            new_capacity = buf->size + realsize + 1024;

        new_data = realloc(buf->data, new_capacity);
        if (new_data == NULL)
            return 0;

        buf->data = new_data;
        buf->capacity = new_capacity;
    }

    memcpy(buf->data + buf->size, contents, realsize);
    buf->size += realsize;

    return realsize;
}

/*
 * A call waits for the node as long as the node takes: nothing here times out
 * but the connect. What ends a wait is the backend's own statement ending - a
 * cancel (pg_cancel_backend, statement_timeout) or a terminate. Returning
 * non-zero makes libcurl reset the call's HTTP/2 stream, so the node drops its
 * side of the call too (the node hangs up on the model and cancels
 * its page query). The caller's CHECK_FOR_INTERRUPTS() then raises the cancel,
 * once libcurl has returned - an ERROR must never unwind through libcurl.
 *
 * A cancel is heard only where the backend would act on it: not while
 * interrupts are held, as they are for the COMMIT PREPARED and ROLLBACK
 * calls of yt_cluster_xact_callback(). A terminate is heard there too - with no
 * timeout left, it is the one way out of a wait on a node that never answers.
 * libcurl calls this about once a second even when nothing moves.
 */
static int
cancel_check(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
             curl_off_t ultotal, curl_off_t ulnow)
{
    if (ProcDiePending)
        return 1;
    return (QueryCancelPending && INTERRUPTS_CAN_BE_PROCESSED()) ? 1 : 0;
}

/* [ycoord] a time limit on each call from now on, ms; 0 none (yt_cluster_grpc_call_timeout) */
static long g_call_timeout_ms = 0;

void
yt_cluster_grpc_call_timeout(long ms)
{
    g_call_timeout_ms = ms > 0 ? ms : 0;
}

/*
 * Initialize curl handle
 */
static int
init_curl(void)
{
    if (g_curl != NULL)
        return 0;

    curl_global_init(CURL_GLOBAL_ALL);
    g_curl = curl_easy_init();

    if (g_curl == NULL)
    {
        snprintf(g_last_error, sizeof(g_last_error), "Failed to initialize libcurl");
        return -1;
    }

    return 0;
}

/*
 * Build gRPC frame with length-prefixed message
 * Format: 1 byte compressed flag + 4 bytes big-endian length + message
 */
static uint8_t *
build_grpc_frame(const uint8_t *msg, size_t msg_len, size_t *frame_len)
{
    uint8_t *frame;
    size_t total_len = GRPC_HEADER_SIZE + msg_len;

    frame = malloc(total_len);
    if (frame == NULL)
        return NULL;

    /* Compression flag (0 = not compressed) */
    frame[0] = 0;

    /* Message length in big-endian */
    frame[1] = (msg_len >> 24) & 0xFF;
    frame[2] = (msg_len >> 16) & 0xFF;
    frame[3] = (msg_len >> 8) & 0xFF;
    frame[4] = msg_len & 0xFF;

    /* Copy message */
    if (msg_len > 0)
        memcpy(frame + GRPC_HEADER_SIZE, msg, msg_len);

    *frame_len = total_len;
    return frame;
}

/*
 * Parse gRPC frame and extract message
 */
static int
parse_grpc_frame(const uint8_t *frame, size_t frame_len, uint8_t **msg, size_t *msg_len)
{
    uint32_t len;

    if (frame_len < GRPC_HEADER_SIZE)
    {
        snprintf(g_last_error, sizeof(g_last_error), "Invalid gRPC frame: too short (%zu bytes)", frame_len);
        return -1;
    }

    /* Check compression flag */
    if (frame[0] != 0)
    {
        snprintf(g_last_error, sizeof(g_last_error), "Compressed gRPC messages not supported");
        return -1;
    }

    /* Parse message length (big-endian) */
    len = ((uint32_t)frame[1] << 24) |
          ((uint32_t)frame[2] << 16) |
          ((uint32_t)frame[3] << 8) |
          (uint32_t)frame[4];

    if (frame_len < GRPC_HEADER_SIZE + len)
    {
        snprintf(g_last_error, sizeof(g_last_error), "Invalid gRPC frame: incomplete message");
        return -1;
    }

    *msg = (uint8_t *)(frame + GRPC_HEADER_SIZE);
    *msg_len = len;

    return 0;
}

/*
 * Make gRPC call
 */
static int
grpc_call(const char *method, const uint8_t *req_data, size_t req_len,
          uint8_t **resp_data, size_t *resp_len)
{
    CURLcode res;
    struct curl_slist *headers = NULL;
    ResponseBuffer response = {0};
    char url[512];
    uint8_t *frame;
    size_t frame_len;
    long http_code;
    uint8_t *resp_copy;
    uint64 t_enter, t_locked, t_setup, t_perform;

    prof_init();
    t_enter = prof_now();

    pthread_mutex_lock(&g_curl_mutex);

    t_locked = prof_now();

    if (g_curl == NULL || strlen(g_host) == 0)
    {
        snprintf(g_last_error, sizeof(g_last_error), "Not connected");
        pthread_mutex_unlock(&g_curl_mutex);
        return -1;
    }

    /* Determine if Unix socket and build URL before any curl setopt.
     * Never pass a unix:// scheme to curl - it does not support that scheme.
     * Unix sockets are configured via CURLOPT_UNIX_SOCKET_PATH + http://localhost URL. */
    {
        const char *socket_path = NULL;
        if (strncmp(g_host, "unix://", 7) == 0)
            socket_path = g_host + 7;
        else if (g_host[0] == '/')
            socket_path = g_host;

        if (socket_path != NULL)
            snprintf(url, sizeof(url), "http://localhost%s", method);
        else
            snprintf(url, sizeof(url), "%s%s", g_host, method);
    }

    /* Initialize response buffer */
    response.capacity = 4096;
    response.data = malloc(response.capacity);
    if (response.data == NULL)
    {
        snprintf(g_last_error, sizeof(g_last_error), "Memory allocation failed");
        pthread_mutex_unlock(&g_curl_mutex);
        return -1;
    }
    response.size = 0;

    /* Build gRPC frame */
    frame = build_grpc_frame(req_data, req_len, &frame_len);
    if (frame == NULL)
    {
        free(response.data);
        snprintf(g_last_error, sizeof(g_last_error), "Failed to build gRPC frame");
        pthread_mutex_unlock(&g_curl_mutex);
        return -1;
    }

    /* Set headers for gRPC */
    headers = curl_slist_append(headers, "Content-Type: application/grpc");
    headers = curl_slist_append(headers, "TE: trailers");
    headers = curl_slist_append(headers, "grpc-encoding: identity");

    /* reset captured trailers for this call */
    g_grpc_status = -1;
    g_grpc_message[0] = '\0';

    /* Configure curl */
    curl_easy_reset(g_curl);
    curl_easy_setopt(g_curl, CURLOPT_URL, url);
    /* [ycoord] a call that must end even with interrupts held: the cancel of a wait, the
     * release at a transaction's or a session's end */
    if (g_call_timeout_ms > 0)
        curl_easy_setopt(g_curl, CURLOPT_TIMEOUT_MS, g_call_timeout_ms);
    /* capture grpc-status / grpc-message from headers+trailers */
    curl_easy_setopt(g_curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(g_curl, CURLOPT_HEADERDATA, NULL);
    /* gRPC requires HTTP/2. For Unix socket (cleartext h2c) use PRIOR_KNOWLEDGE -
     * tonic speaks h2c directly without the HTTP/1.1 Upgrade dance. */
    curl_easy_setopt(g_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE);
    curl_easy_setopt(g_curl, CURLOPT_POST, 1L);
    curl_easy_setopt(g_curl, CURLOPT_POSTFIELDS, frame);
    curl_easy_setopt(g_curl, CURLOPT_POSTFIELDSIZE, (long)frame_len);
    curl_easy_setopt(g_curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(g_curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(g_curl, CURLOPT_WRITEDATA, &response);
    /* connecting is the only thing with a time limit: the call itself runs
     * until the node answers or the statement is cancelled (cancel_check) */
    curl_easy_setopt(g_curl, CURLOPT_CONNECTTIMEOUT_MS, (long)YT_CLUSTER_CONNECT_TIMEOUT_MS);
    curl_easy_setopt(g_curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(g_curl, CURLOPT_XFERINFOFUNCTION, cancel_check);
    /* Without this libcurl saves/restores the SIGPIPE disposition (and
     * arms SIGALRM for the connect timeout above) around every transfer: strace of a
     * pgbench backend showed 36 rt_sigaction per transaction - the single
     * most-called syscall, and none of it moves data. We are always on a
     * single backend thread holding g_curl_mutex, so signal safety is ours to
     * give up. Note this makes the *connect* timeout resolver-dependent, which
     * is irrelevant for a unix socket. */
    curl_easy_setopt(g_curl, CURLOPT_NOSIGNAL, 1L);

    /* Set Unix socket path if applicable */
    if (strncmp(g_host, "unix://", 7) == 0)
        curl_easy_setopt(g_curl, CURLOPT_UNIX_SOCKET_PATH, g_host + 7);
    else if (g_host[0] == '/')
        curl_easy_setopt(g_curl, CURLOPT_UNIX_SOCKET_PATH, g_host);

    t_setup = prof_now();

    /* Perform request */
    res = curl_easy_perform(g_curl);

    t_perform = prof_now();

    /* Cleanup */
    curl_slist_free_all(headers);
    free(frame);

    if (res != CURLE_OK)
    {
        if (res == CURLE_ABORTED_BY_CALLBACK)
            snprintf(g_last_error, sizeof(g_last_error), "canceled");
        else
            snprintf(g_last_error, sizeof(g_last_error), "curl error: %s",
                     curl_easy_strerror(res));
        free(response.data);
        pthread_mutex_unlock(&g_curl_mutex);
        return -1;
    }

    /* Check HTTP status */
    curl_easy_getinfo(g_curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200)
    {
        snprintf(g_last_error, sizeof(g_last_error), "HTTP error: %ld", http_code);
        free(response.data);
        pthread_mutex_unlock(&g_curl_mutex);
        return -1;
    }

    /* A non-zero grpc-status is a real server error with an
     * empty body - surface grpc-message instead of "Invalid gRPC frame". */
    if (g_grpc_status > 0)
    {
        if (g_grpc_message[0] != '\0')
            snprintf(g_last_error, sizeof(g_last_error), "%s", g_grpc_message);
        else
            snprintf(g_last_error, sizeof(g_last_error), "gRPC status %ld", g_grpc_status);
        free(response.data);
        pthread_mutex_unlock(&g_curl_mutex);
        return -1;
    }

    /* Parse response frame */
    if (parse_grpc_frame(response.data, response.size, resp_data, resp_len) != 0)
    {
        free(response.data);
        pthread_mutex_unlock(&g_curl_mutex);
        return -1;
    }

    /* Caller needs to copy the data as we'll free the buffer */
    resp_copy = malloc(*resp_len);
    if (resp_copy == NULL)
    {
        snprintf(g_last_error, sizeof(g_last_error), "Memory allocation failed");
        free(response.data);
        pthread_mutex_unlock(&g_curl_mutex);
        return -1;
    }
    memcpy(resp_copy, *resp_data, *resp_len);
    *resp_data = resp_copy;

    free(response.data);

    if (g_prof_every > 0)
    {
        g_prof_calls++;
        g_prof_ns_lock += t_locked - t_enter;
        g_prof_ns_setup += t_setup - t_locked;
        g_prof_ns_perform += t_perform - t_setup;
        g_prof_ns_post += prof_now() - t_perform;
        g_prof_ns_codec += g_prof_pending_codec;
        g_prof_pending_codec = 0;
        prof_report();
    }

    pthread_mutex_unlock(&g_curl_mutex);

    return 0;
}

/*
 * Connect to gRPC server
 */
int
yt_cluster_grpc_connect(const char *host)
{
    pthread_mutex_lock(&g_curl_mutex);

    if (init_curl() != 0)
    {
        pthread_mutex_unlock(&g_curl_mutex);
        return -1;
    }

    strncpy(g_host, host, YT_CLUSTER_MAX_HOST_LEN - 1);
    g_host[YT_CLUSTER_MAX_HOST_LEN - 1] = '\0';

    pthread_mutex_unlock(&g_curl_mutex);

    return 0;
}

/*
 * Disconnect from gRPC server
 */
void
yt_cluster_grpc_disconnect(void)
{
    pthread_mutex_lock(&g_curl_mutex);

    if (g_curl != NULL)
    {
        curl_easy_cleanup(g_curl);
        g_curl = NULL;
    }

    g_host[0] = '\0';
    curl_global_cleanup();

    pthread_mutex_unlock(&g_curl_mutex);
}

/*
 * Get last error message
 */
const char *
yt_cluster_grpc_last_error(void)
{
    return g_last_error;
}

/*
 * Make modify RPC call (LocalGrpc.modify)
 */
int
yt_cluster_grpc_modify(const DbInputEntity *req, ModifyReply *resp)
{
    uint8_t *req_data;
    size_t req_len;
    uint8_t *resp_data = NULL;
    size_t resp_len;
    int ret;
    uint64 t0;

    /* encode is folded into this call's `codec`; decode lands on the
     * next one's. Over a reporting window of many calls that is the same
     * average, and it keeps the accounting out of the mutex. */
    prof_init();
    t0 = prof_now();

    /* [YT_CALL] Per-call start/end markers in the POSTGRES log, so a call can be
     * lined up by wall-clock timestamp against the ytserv log's [YT_CHAIN] lines
     * for the same tx. The gap between our "start" and the server's first line is
     * transport + dispatch; the gap between the server's last line and our "done"
     * is the reply leg. Only emitted when YT_CLUSTER_PROF is set. */
    if (g_prof_every > 0)
        elog(LOG, "[YT_CALL] modify tx=%lu start", (unsigned long) req->tx);

    /* Encode request */
    req_len = yt_cluster_pb_encode_db_input_entity(req, &req_data);
    if (req_data == NULL)
    {
        snprintf(g_last_error, sizeof(g_last_error), "Failed to encode request");
        return -1;
    }

    g_prof_pending_codec += prof_now() - t0;

    /* Make call */
    ret = grpc_call("/db.LocalGrpc/modify", req_data, req_len, &resp_data, &resp_len);
    free(req_data);

    if (ret != 0)
        return ret;

    t0 = prof_now();

    /* Decode response */
    ret = yt_cluster_pb_decode_modify_reply(resp_data, resp_len, resp);
    free(resp_data);

    g_prof_pending_codec += prof_now() - t0;

    if (g_prof_every > 0)
        elog(LOG, "[YT_CALL] modify tx=%lu done skip_complete=%d",
             (unsigned long) req->tx, (int) resp->skip_complete);

    return ret;
}

/*
 * Make complete RPC call (LocalGrpc.complete)
 */
int
yt_cluster_grpc_complete(const ClusterComplete *req, CompleteReply *resp)
{
    uint8_t *req_data;
    size_t req_len;
    uint8_t *resp_data = NULL;
    size_t resp_len;
    int ret;
    uint64 t0;

    prof_init();
    t0 = prof_now();

    /* See the note in yt_cluster_grpc_modify(). `commit` is the 2PC phase:
     * 0 = PREPARE, 1 = COMMIT, 2 = ROLLBACK. */
    if (g_prof_every > 0)
        elog(LOG, "[YT_CALL] complete tx=%lu commit=%d start",
             (unsigned long) req->txid, (int) req->commit);

    /* Encode request */
    req_len = yt_cluster_pb_encode_cluster_complete(req, &req_data);
    if (req_data == NULL)
    {
        snprintf(g_last_error, sizeof(g_last_error), "Failed to encode request");
        return -1;
    }

    g_prof_pending_codec += prof_now() - t0;

    /* Make call */
    ret = grpc_call("/db.LocalGrpc/complete", req_data, req_len, &resp_data, &resp_len);
    free(req_data);

    if (ret != 0)
        return ret;

    t0 = prof_now();

    /* Decode response */
    ret = yt_cluster_pb_decode_complete_reply(resp_data, resp_len, resp);
    free(resp_data);

    g_prof_pending_codec += prof_now() - t0;

    if (g_prof_every > 0)
        elog(LOG, "[YT_CALL] complete tx=%lu commit=%d done",
             (unsigned long) req->txid, (int) req->commit);

    return ret;
}

/*
 * Make info RPC call (LocalGrpc.info)
 */
int
yt_cluster_grpc_info(const InfoRequest *req, InfoResponse *resp)
{
    uint8_t *req_data;
    size_t req_len;
    uint8_t *resp_data = NULL;
    size_t resp_len;
    int ret;

    /* Encode request */
    req_len = yt_cluster_pb_encode_info_request(req, &req_data);
    if (req_data == NULL)
    {
        snprintf(g_last_error, sizeof(g_last_error), "Failed to encode request");
        return -1;
    }

    /* Make call */
    ret = grpc_call("/db.LocalGrpc/info", req_data, req_len, &resp_data, &resp_len);
    free(req_data);

    if (ret != 0)
        return ret;

    /* Decode response */
    ret = yt_cluster_pb_decode_info_response(resp_data, resp_len, resp);
    free(resp_data);

    return ret;
}

/*
 * Make llm RPC call (LocalGrpc.llm): the text-to-SQL model's statement or
 * a page of its rows. Like every call it waits as long as the node takes - the
 * model thinks for minutes on a CPU, a page may run for many more - and a
 * cancel of the statement ends the call on both sides (cancel_check).
 */
int
yt_cluster_grpc_llm(const LlmRequest *req, LlmReply *resp)
{
    uint8_t *req_data;
    size_t req_len;
    uint8_t *resp_data = NULL;
    size_t resp_len;
    int ret;

    req_len = yt_cluster_pb_encode_llm_request(req, &req_data);
    if (req_data == NULL)
    {
        snprintf(g_last_error, sizeof(g_last_error), "Failed to encode request");
        return -1;
    }

    ret = grpc_call("/db.LocalGrpc/llm", req_data, req_len, &resp_data, &resp_len);
    free(req_data);

    if (ret != 0)
        return ret;

    ret = yt_cluster_pb_decode_llm_reply(resp_data, resp_len, resp);
    free(resp_data);
    if (ret != 0)
        snprintf(g_last_error, sizeof(g_last_error), "Failed to decode the llm reply");

    return ret;
}

/*
 * Make merge RPC call (LocalGrpc.merge)
 */
int
yt_cluster_grpc_merge(const MergeRequest *req, InfoResponse *resp)
{
    uint8_t *req_data;
    size_t req_len;
    uint8_t *resp_data = NULL;
    size_t resp_len;
    int ret;

    /* Encode request */
    req_len = yt_cluster_pb_encode_merge_request(req, &req_data);
    if (req_data == NULL)
    {
        snprintf(g_last_error, sizeof(g_last_error), "Failed to encode request");
        return -1;
    }

    /* Make call */
    ret = grpc_call("/db.LocalGrpc/merge", req_data, req_len, &resp_data, &resp_len);
    free(req_data);

    if (ret != 0)
        return ret;

    /* Decode response */
    ret = yt_cluster_pb_decode_info_response(resp_data, resp_len, resp);
    free(resp_data);

    return ret;
}
