#include <common/sys_config.h>

#if CONFIG_ENABLE_AGORA_DATASTREAM

#include <string.h>
#include <stdlib.h>             /* atoi */
#include <components/log.h>
#include <os/os.h>
#include <os/mem.h>

#include "cJSON.h"
#include "mbedtls/base64.h"

#include "sentino_command_bus.h"
#include "sentino_command_agora_source.h"

#define TAG "cmd_agora"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* Mirror of the producer's struct in agora_rtc.c. The queue stores
 * sizeof(char *) bytes per item — the producer pushes a struct holding
 * one pointer, so popping into the same shape recovers the heap pointer
 * that owns the JSON bytes. Producer and consumer must keep this in sync. */
typedef struct {
    char *data;
} conv_ai_stream_msg_t;

/* The queue itself is owned by bk_smart_config_sentino_adapter.c — we just
 * borrow it. Producer (agora_rtc.c) also reaches it via extern. */
extern beken_queue_t datastream_queue;

static beken_thread_t s_worker_thread = NULL;

/* Wire format on the Agora datastream is NOT raw JSON — it's ConvoAI's
 * chunked envelope:
 *
 *   <msgid_hex_8>|<frag_idx>|<frag_total>|<base64_chunk>
 *
 * Single-fragment messages are decoded inline. Multi-fragment messages
 * accumulate b64 chunks under one msgid (file-static state below) until
 * idx == total, then base64-decode the concatenation, strip the ConvoAI
 * publish-message envelope, and forward the inner Sentino `content` to
 * the bus.
 *
 * Decoded JSON is `{"object":"...", "content":{...}, ...}` directly —
 * no {uid, payload:...} wrapper. Same channel carries message.user
 * (commands), message.state, user.transcription, assistant.transcription;
 * only message.user is forwarded.
 *
 * Real-world traffic on this firmware sees both 1-frag and 2-frag messages
 * (~713B head + variable tail). MAX_FRAG_TOTAL bounds runaway state if a
 * malformed header claims a huge total; MAX_ACCUM_LEN bounds the b64 buffer.
 */

#define MAX_FRAG_TOTAL   8
#define MAX_ACCUM_LEN    (8 * 1024)

/* Reassembly state. Worker is single-threaded so no lock needed.
 * Invariant: s_accum_buf != NULL  ⇔  s_accum_total != 0  ⇔  reassembly in progress. */
static char    s_accum_msgid[9];     /* 8 hex chars + NUL */
static int     s_accum_total;        /* 0 = idle */
static int     s_accum_received;     /* fragments accumulated so far */
static char   *s_accum_buf;          /* psram, holds concatenated b64 */
static size_t  s_accum_len;          /* bytes used in s_accum_buf */

static void accum_reset(void)
{
    if (s_accum_buf) {
        psram_free(s_accum_buf);
        s_accum_buf = NULL;
    }
    s_accum_msgid[0] = '\0';
    s_accum_total    = 0;
    s_accum_received = 0;
    s_accum_len      = 0;
}

/* base64-decode, JSON-parse, strip ConvoAI publish envelope, hand inner
 * Sentino content to the bus. Frees nothing the caller owns; returns
 * nothing — failures are logged. */
static void decode_and_forward(const char *b64, size_t b64_len)
{
    size_t out_len = 0;
    int    rc      = mbedtls_base64_decode(NULL, 0, &out_len,
                                           (const unsigned char *)b64, b64_len);
    if (rc != 0 && rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        LOGW("base64 probe fail rc=-0x%04x b64_len=%u\n", -rc, (unsigned)b64_len);
        return;
    }
    if (out_len == 0) { return; }

    unsigned char *decoded = (unsigned char *)psram_zalloc(out_len + 1);
    if (!decoded) {
        LOGW("psram alloc fail for decoded %u\n", (unsigned)out_len);
        return;
    }
    rc = mbedtls_base64_decode(decoded, out_len, &out_len,
                               (const unsigned char *)b64, b64_len);
    if (rc != 0) {
        LOGW("base64 decode fail rc=-0x%04x\n", -rc);
        psram_free(decoded);
        return;
    }
    decoded[out_len] = '\0';

    cJSON *root = cJSON_Parse((const char *)decoded);
    if (!root) {
        LOGW("outer json parse fail prefix=%.80s\n", (const char *)decoded);
        psram_free(decoded);
        return;
    }

    /* ConvoAI L2 filter: only message.user carries device commands.
     * message.state / user.transcription / assistant.transcription are not
     * for us — silent skip to avoid log flood. */
    const char *obj = cJSON_GetStringValue(
                          cJSON_GetObjectItemCaseSensitive(root, "object"));
    if (!obj || strcmp(obj, "message.user") != 0) {
        goto done;
    }

    /* Strip ConvoAI publish envelope: pass the inner `content` (Sentino
     * schema) to the bus, which does its own type/actions parsing. */
    const cJSON *content = cJSON_GetObjectItemCaseSensitive(root, "content");
    if (!cJSON_IsObject(content)) {
        LOGW("message.user content not object, type=%d\n",
             content ? content->type : -1);
        goto done;
    }

    sentino_command_bus_dispatch_content(content);

done:
    cJSON_Delete(root);
    psram_free(decoded);
}

/* Top-level frame handler: deframe, reassemble multi-fragment messages,
 * hand the assembled b64 payload to decode_and_forward. */
static void parse_and_forward(const char *frame)
{
    const char *p1 = strchr(frame,    '|');
    if (!p1) { LOGW("frame missing sep1\n"); return; }
    const char *p2 = strchr(p1 + 1, '|');
    if (!p2) { LOGW("frame missing sep2\n"); return; }
    const char *p3 = strchr(p2 + 1, '|');
    if (!p3) { LOGW("frame missing sep3\n"); return; }

    int idx     = atoi(p1 + 1);
    int total   = atoi(p2 + 1);
    const char *b64     = p3 + 1;
    size_t      b64_len = strlen(b64);

    if (total < 1 || total > MAX_FRAG_TOTAL || idx < 1 || idx > total) {
        LOGW("bad frag header idx=%d total=%d msgid=%.8s\n", idx, total, frame);
        accum_reset();
        return;
    }

    /* Single-fragment fast path — no buffering needed. */
    if (total == 1) {
        if (s_accum_total != 0) {
            /* In-progress reassembly got interrupted by a single-frag msg.
             * Drop the partial state — we lost that multi-frag message. */
            LOGW("drop partial msgid=%s (%d/%d) for inline single-frag\n",
                 s_accum_msgid, s_accum_received, s_accum_total);
            accum_reset();
        }
        decode_and_forward(b64, b64_len);
        return;
    }

    /* Multi-fragment path. */
    if (idx == 1) {
        /* Starting a new accumulation. Drop any leftover partial. */
        if (s_accum_total != 0) {
            LOGW("drop partial msgid=%s (%d/%d) for new msgid=%.8s\n",
                 s_accum_msgid, s_accum_received, s_accum_total, frame);
            accum_reset();
        }
        s_accum_buf = (char *)psram_zalloc(MAX_ACCUM_LEN + 1);
        if (!s_accum_buf) {
            LOGE("accum alloc fail\n");
            return;
        }
        memcpy(s_accum_msgid, frame, 8);
        s_accum_msgid[8] = '\0';
        s_accum_total    = total;
        s_accum_received = 0;
        s_accum_len      = 0;
    } else {
        /* Continuation. Strict checks: same msgid, same total, idx in order. */
        if (s_accum_total == 0 ||
            s_accum_total != total ||
            memcmp(s_accum_msgid, frame, 8) != 0 ||
            idx != s_accum_received + 1) {
            LOGW("frag mismatch got msgid=%.8s idx=%d total=%d, expected msgid=%s idx=%d total=%d\n",
                 frame, idx, total,
                 s_accum_msgid[0] ? s_accum_msgid : "(none)",
                 s_accum_received + 1, s_accum_total);
            accum_reset();
            return;
        }
    }

    if (s_accum_len + b64_len > MAX_ACCUM_LEN) {
        LOGW("accum overflow at idx=%d/%d msgid=%s, drop\n",
             idx, total, s_accum_msgid);
        accum_reset();
        return;
    }
    memcpy(s_accum_buf + s_accum_len, b64, b64_len);
    s_accum_len += b64_len;
    s_accum_buf[s_accum_len] = '\0';
    s_accum_received++;

    if (s_accum_received < s_accum_total) {
        return;     /* wait for more fragments */
    }

    LOGI("reassembled msgid=%s frags=%d b64_len=%u\n",
         s_accum_msgid, s_accum_total, (unsigned)s_accum_len);
    decode_and_forward(s_accum_buf, s_accum_len);
    accum_reset();
}

static void agora_source_thread(beken_thread_arg_t arg)
{
    LOGW("worker started\n");      /* LOGW so it survives default INFO-level filtering */
    (void)arg;

    while (1) {
        conv_ai_stream_msg_t msg = {0};
        int ret = rtos_pop_from_queue(&datastream_queue, &msg, BEKEN_WAIT_FOREVER);
        if (ret != BK_OK) {
            continue;
        }
        if (!msg.data) {
            /* Producer's psram_zalloc may have failed; pointer is NULL. */
            continue;
        }

        parse_and_forward(msg.data);
        psram_free(msg.data);
    }
}

int sentino_command_agora_source_init(void)
{
    if (s_worker_thread) {
        return 0;       /* idempotent */
    }
    if (!datastream_queue) {
        LOGE("datastream_queue not ready, call bk_sconf_init_datastream_resource() first\n");
        return -1;
    }

    int ret = rtos_create_thread(&s_worker_thread,
                                 BEKEN_DEFAULT_WORKER_PRIORITY,
                                 "cmd_agora",
                                 agora_source_thread,
                                 1024 * 4,
                                 NULL);
    if (ret != BK_OK) {
        LOGE("create worker thread failed: %d\n", ret);
        s_worker_thread = NULL;
        return -1;
    }
    return 0;
}

#endif /* CONFIG_ENABLE_AGORA_DATASTREAM */
