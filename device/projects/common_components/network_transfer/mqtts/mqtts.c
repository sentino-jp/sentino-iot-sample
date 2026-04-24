/*
 * mqtts — generic MQTT 3.1.1 client (plain TCP + TLS)
 * See mqtts.h for the rationale and API.
 */

#include <os/os.h>
#include <os/mem.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>   /* for time(NULL) — diagnostic; mbedtls cert verify uses same source */

#include <components/log.h>

#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "tls_connect.h"   /* MbedTLSSession typedef only */

#include "MQTTPacket.h"

#include "mqtts.h"

#define TAG "mqtts"

/* Temp: bump LOGI to W so default log level shows handshake/CONNACK/SUB/PUB
 * during bring-up. Revert to BK_LOGI once flow is stable. */
#define LOGI(fmt, ...) BK_LOGW(TAG, fmt "\n", ##__VA_ARGS__)
#define LOGW(fmt, ...) BK_LOGW(TAG, fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) BK_LOGE(TAG, fmt "\n", ##__VA_ARGS__)
#define LOGD(fmt, ...) BK_LOGD(TAG, fmt "\n", ##__VA_ARGS__)

/* ─────────────────────────── transport layer ─────────────────────────── */

typedef struct {
    int  (*read) (void *ctx, uint8_t *buf, size_t n);  /* <0 err, 0 timeout, >0 bytes */
    int  (*write)(void *ctx, const uint8_t *buf, size_t n);
    void (*close)(void *ctx);                          /* close socket only — safe on dead WiFi */
} transport_ops_t;

/* TLS path uses MbedTLSSession from psa_mbedtls/tls_connect.h.
 * Plain TCP path uses bare mbedtls_net_context. */
typedef struct {
    bool                use_tls;
    /* TLS */
    MbedTLSSession     *tls;        /* heap-allocated; close routine frees host/port/buffer/self */
    mbedtls_x509_crt    cacert;     /* CA chain — owned by us (NOT MbedTLSSession::cacert,
                                     * which is only present when USE_CA_CERTIFICATE_EN=1
                                     * in SDK config — a wrapper-internal flag we don't want
                                     * to depend on). */
    bool                cacert_inited;
    /* Plain TCP */
    mbedtls_net_context tcp;
    char               *tcp_host;   /* heap copies, freed by us */
    char               *tcp_port;
    uint16_t            read_timeout_ms;
} transport_t;

static int tls_read_fn(void *ctx, uint8_t *buf, size_t n)
{
    transport_t *t = ctx;
    int r = mbedtls_ssl_read(&t->tls->ssl, buf, n);
    /* Nonblock: return 0 to mean "no data, try later" so reader releases mutex
     * quickly. BK LWIP select() in mbedtls_net_recv_timeout was not honoring
     * 100ms timeout, causing reader to hold io_mutex indefinitely and starve
     * publishers. */
    if (r == MBEDTLS_ERR_SSL_TIMEOUT ||
        r == MBEDTLS_ERR_SSL_WANT_READ ||
        r == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return 0;
    }
    return (r < 0) ? -1 : r;
}

static int tls_write_fn(void *ctx, const uint8_t *buf, size_t n)
{
    transport_t *t = ctx;
    size_t off = 0;
    while (off < n) {
        int w = mbedtls_ssl_write(&t->tls->ssl, buf + off, n - off);
        if (w == MBEDTLS_ERR_SSL_WANT_READ || w == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (w < 0) return -1;
        off += (size_t)w;
    }
    return (int)n;
}

static void tls_close_fn(void *ctx)
{
    transport_t *t = ctx;
    if (t->tls) {
        /* Close socket but DO NOT call mbedtls_client_close — that calls
         * ssl_close_notify which hangs on dead WiFi and also frees the
         * session itself, which we want to defer until destroy(). */
        mbedtls_net_free(&t->tls->server_fd);
    }
}

static int tcp_read_fn(void *ctx, uint8_t *buf, size_t n)
{
    transport_t *t = ctx;
    /* socket is nonblock (set in tcp_open). recv returns WANT_READ when no
     * data; map to 0 so reader releases mutex immediately. */
    int r = mbedtls_net_recv(&t->tcp, buf, n);
    if (r == MBEDTLS_ERR_SSL_TIMEOUT ||
        r == MBEDTLS_ERR_SSL_WANT_READ) {
        return 0;
    }
    return (r < 0) ? -1 : r;
}

static int tcp_write_fn(void *ctx, const uint8_t *buf, size_t n)
{
    transport_t *t = ctx;
    size_t off = 0;
    while (off < n) {
        int w = mbedtls_net_send(&t->tcp, buf + off, n - off);
        if (w == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (w < 0) return -1;
        off += (size_t)w;
    }
    return (int)n;
}

static void tcp_close_fn(void *ctx)
{
    transport_t *t = ctx;
    mbedtls_net_free(&t->tcp);
}

static const transport_ops_t TLS_OPS = { tls_read_fn, tls_write_fn, tls_close_fn };
static const transport_ops_t TCP_OPS = { tcp_read_fn, tcp_write_fn, tcp_close_fn };

/* Manually free everything tls_open inits. Replaces SDK's mbedtls_client_close
 * which (a) calls ssl_close_notify (would hang on dead WiFi) and (b) frees the
 * SDK-private heap layout we don't fully own. We own everything we init'd here
 * and free it ourselves.
 *
 * Note: cacert is owned by transport_t (not MbedTLSSession) — see transport_t
 * comment. Caller must free t->cacert separately if t->cacert_inited. */
static void tls_destroy(MbedTLSSession *s)
{
    if (!s) return;
    mbedtls_ssl_free(&s->ssl);
    mbedtls_ssl_config_free(&s->conf);
    mbedtls_ctr_drbg_free(&s->ctr_drbg);
    mbedtls_entropy_free(&s->entropy);
    mbedtls_net_free(&s->server_fd);
    if (s->host)   { os_free(s->host);   s->host = NULL; }
    if (s->port)   { os_free(s->port);   s->port = NULL; }
    if (s->buffer) { os_free(s->buffer); s->buffer = NULL; }
    os_free(s);
}

/* Tolerated verify flags: time-validity errors only. With device clock = 1970
 * (no NTP yet), BADCERT_FUTURE is expected. After NTP integration this mask
 * becomes 0 (effectively VERIFY_REQUIRED). See plans/tls-verification-explainer.md */
#define MQTTS_TOLERATED_VERIFY_FLAGS  \
    (MBEDTLS_X509_BADCERT_FUTURE | MBEDTLS_X509_BADCERT_EXPIRED)

/* TLS open: hand-rolled mbedtls init (bypassing SDK tls_connect.c which hardcodes
 * VERIFY_NONE). When ca_pem != NULL, mode is OPTIONAL with selective reject:
 *   - chain/hostname/usage failures abort the connection
 *   - time-validity failures (FUTURE/EXPIRED) are tolerated and logged
 * out_verify_flags receives the verify_result snapshot for stats. */
static int tls_open(transport_t *t, const char *host, uint16_t port,
                    uint16_t read_timeout_ms, uint16_t connect_timeout_ms,
                    const char *ca_pem, uint32_t *out_verify_flags)
{
    if (out_verify_flags) *out_verify_flags = 0;

    /* Diagnostic: log the device clock that mbedtls will use for cert
     * notBefore/notAfter check. 1970 = 0, 2026-04 ≈ 1776e6. If this is 0
     * but VERIFY succeeds anyway, time-validity is not actually checked. */
    LOGW("tls_open: device time(NULL) = %lld", (long long)time(NULL));

    MbedTLSSession *s = (MbedTLSSession *)os_zalloc(sizeof(MbedTLSSession));
    if (!s) return -1;

    s->host = (char *)os_malloc(strlen(host) + 1);
    s->port = (char *)os_malloc(8);
    s->buffer_len = 2048;
    s->buffer = (unsigned char *)os_malloc(s->buffer_len);
    if (!s->host || !s->port || !s->buffer) goto fail_early;
    strcpy(s->host, host);
    snprintf(s->port, 8, "%u", port);

    /* mbedtls init — equivalent to mbedtls_client_init but without SDK lifecycle. */
    mbedtls_net_init(&s->server_fd);
    mbedtls_ssl_init(&s->ssl);
    mbedtls_ssl_config_init(&s->conf);
    mbedtls_ctr_drbg_init(&s->ctr_drbg);
    mbedtls_entropy_init(&s->entropy);

    static const char *PERS = "mqtts";
    int rc = mbedtls_ctr_drbg_seed(&s->ctr_drbg, mbedtls_entropy_func, &s->entropy,
                                   (const unsigned char *)PERS, strlen(PERS));
    if (rc != 0) { LOGE("ctr_drbg_seed: -0x%x", -rc); goto fail; }

    /* Parse CA chain if provided — use our own cacert in transport_t (not
     * MbedTLSSession::cacert which is gated by SDK config USE_CA_CERTIFICATE_EN). */
    if (ca_pem) {
        mbedtls_x509_crt_init(&t->cacert);
        t->cacert_inited = true;
        rc = mbedtls_x509_crt_parse(&t->cacert,
                                    (const unsigned char *)ca_pem,
                                    strlen(ca_pem) + 1);  /* +1 for trailing \0 */
        if (rc != 0) { LOGE("ca_pem parse: -0x%x", -rc); goto fail; }
    }

    rc = mbedtls_ssl_config_defaults(&s->conf,
                                     MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) { LOGE("ssl_config_defaults: -0x%x", -rc); goto fail; }

    /* OPTIONAL mode: handshake doesn't auto-abort; we read flags after and
     * decide. This lets us tolerate time-validity errors without sacrificing
     * chain/hostname enforcement. */
    mbedtls_ssl_conf_authmode(&s->conf,
        ca_pem ? MBEDTLS_SSL_VERIFY_OPTIONAL : MBEDTLS_SSL_VERIFY_NONE);
    if (ca_pem) mbedtls_ssl_conf_ca_chain(&s->conf, &t->cacert, NULL);
    mbedtls_ssl_conf_rng(&s->conf, mbedtls_ctr_drbg_random, &s->ctr_drbg);
    /* Handshake budget — single ssl_read can take up to this long while
     * cert chain is delivered. Post-handshake we switch bio so this no longer
     * applies to runtime reads. */
    mbedtls_ssl_conf_read_timeout(&s->conf, connect_timeout_ms);

    rc = mbedtls_ssl_setup(&s->ssl, &s->conf);
    if (rc != 0) { LOGE("ssl_setup: -0x%x", -rc); goto fail; }

    /* Hostname for SNI + SAN check. Required even with VERIFY_NONE for SNI. */
    rc = mbedtls_ssl_set_hostname(&s->ssl, host);
    if (rc != 0) { LOGE("ssl_set_hostname: -0x%x", -rc); goto fail; }

    rc = mbedtls_net_connect(&s->server_fd, s->host, s->port, MBEDTLS_NET_PROTO_TCP);
    if (rc != 0) { LOGE("net_connect: -0x%x", -rc); goto fail; }

    /* Handshake stage: blocking + recv_timeout via bio for cert chain delivery. */
    mbedtls_ssl_set_bio(&s->ssl, &s->server_fd,
                        mbedtls_net_send, NULL, mbedtls_net_recv_timeout);

    while ((rc = mbedtls_ssl_handshake(&s->ssl)) != 0) {
        if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
            LOGE("ssl_handshake: -0x%x", -rc);
            goto fail;
        }
    }

    /* OPTIONAL mode: handshake completed. Inspect verify_result and decide. */
    if (ca_pem) {
        uint32_t flags = mbedtls_ssl_get_verify_result(&s->ssl);
        if (out_verify_flags) *out_verify_flags = flags;

        if (flags == 0) {
            LOGW("TLS handshake OK (server cert VERIFIED)");
        } else {
            uint32_t fatal = flags & ~(uint32_t)MQTTS_TOLERATED_VERIFY_FLAGS;
            char vbuf[256];
            mbedtls_x509_crt_verify_info(vbuf, sizeof vbuf, "  ! ", flags);
            if (fatal != 0) {
                LOGE("TLS handshake REJECTED — fatal verify flags 0x%x:\n%s",
                     (unsigned)flags, vbuf);
                goto fail;
            }
            LOGW("TLS handshake OK with TOLERATED verify flags 0x%x:\n%s",
                 (unsigned)flags, vbuf);
        }
    } else {
        LOGW("TLS handshake OK (VERIFY_NONE — server cert NOT validated)");
    }

    /* Post-handshake: nonblock + recv (no timeout) so reader's ssl_read returns
     * WANT_READ immediately when no data, keeping io_mutex hold time <1ms.
     * Otherwise reader blocks in select() up to connect_timeout_ms — see坑4. */
    mbedtls_net_set_nonblock(&s->server_fd);
    mbedtls_ssl_set_bio(&s->ssl, &s->server_fd,
                        mbedtls_net_send, mbedtls_net_recv, NULL);

    t->tls = s;
    t->use_tls = true;
    t->read_timeout_ms = read_timeout_ms;
    return 0;

fail_early:
    if (s->host)   os_free(s->host);
    if (s->port)   os_free(s->port);
    if (s->buffer) os_free(s->buffer);
    os_free(s);
    return -1;
fail:
    tls_destroy(s);
    if (t->cacert_inited) {
        mbedtls_x509_crt_free(&t->cacert);
        t->cacert_inited = false;
    }
    return -1;
}

/* Plain TCP open */
static int tcp_open(transport_t *t, const char *host, uint16_t port,
                    uint16_t read_timeout_ms)
{
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port);

    t->tcp_host = (char *)os_malloc(strlen(host) + 1);
    t->tcp_port = (char *)os_malloc(8);
    if (!t->tcp_host || !t->tcp_port) goto fail;
    strcpy(t->tcp_host, host);
    strcpy(t->tcp_port, port_str);

    mbedtls_net_init(&t->tcp);
    int rc = mbedtls_net_connect(&t->tcp, host, port_str, MBEDTLS_NET_PROTO_TCP);
    if (rc != 0) {
        LOGE("mbedtls_net_connect: -0x%x", -rc);
        goto fail;
    }
    /* Nonblock so tcp_read_fn returns immediately when no data; reader sleeps
     * outside the mutex. mbedtls_net_set_block + recv_timeout was unreliable
     * on BK LWIP (select didn't timeout). */
    mbedtls_net_set_nonblock(&t->tcp);

    t->use_tls = false;
    t->read_timeout_ms = read_timeout_ms;
    LOGI("TCP connected %s:%u", host, port);
    return 0;

fail:
    if (t->tcp_host) { os_free(t->tcp_host); t->tcp_host = NULL; }
    if (t->tcp_port) { os_free(t->tcp_port); t->tcp_port = NULL; }
    return -1;
}

/* ─────────────────────────── client struct ─────────────────────────── */

typedef enum {
    ST_IDLE = 0,
    ST_CONNECTED,
    ST_RECONNECTING,
    ST_DISCONNECTING,
} mqtts_state_t;

typedef struct {
    char    *topic;     /* heap copy; NULL = empty slot */
    uint8_t  qos;
} mqtts_sub_entry_t;

struct mqtts {
    mqtts_config_t      cfg;
    /* host/client_id/username/password are deep-copied into these */
    char               *host;
    char               *client_id;
    char               *username;
    char               *password;

    transport_t         tr;
    const transport_ops_t *ops;

    beken_mutex_t       io_mutex;       /* serializes ops->read / ops->write */
    uint8_t            *rx_buf;
    uint8_t            *tx_buf;

    /* Reader task — long-lived: owns connect + read + reconnect */
    beken_thread_t      reader_thread;
    beken_semaphore_t   reader_exit_sem;
    beken_semaphore_t   first_connect_sem;   /* mqtts_connect waits on this */
    beken_semaphore_t   backoff_wakeup_sem;  /* set by disconnect → cancels backoff sleep */
    int                 first_connect_result;/* 0 ok, <0 fail */
    bool                first_connect_done;  /* once true, reader stops touching first_connect_sem */
    bool                reader_run;

    /* MQTT state */
    mqtts_state_t       state;
    uint16_t            next_packet_id;
    uint32_t            last_send_ms;

    /* Subscription book — replayed after every (re)connect */
    mqtts_sub_entry_t  *subs;
    uint8_t             sub_count;

    /* Reconnect bookkeeping */
    uint32_t            cur_backoff_ms;

    /* PINGRESP liveness check — 0 = no outstanding ping */
    uint32_t            last_pingreq_ms;
    uint32_t            last_pingresp_ms;

    /* Stats (see mqtts_stats_t in header) */
    uint32_t            stat_reconnect_count;
    uint32_t            stat_connect_failures;
    uint32_t            stat_rx_packets;
    uint32_t            stat_tx_packets;
    uint32_t            stat_pingresp_misses;
    uint32_t            stat_last_verify_flags;
    uint32_t            connected_since_ms;

    /* Callbacks */
    mqtts_message_cb_t  msg_cb;   void *msg_user;
    mqtts_event_cb_t    evt_cb;   void *evt_user;
};

/* ─────────────────────────── small helpers ─────────────────────────── */

static uint16_t next_pid(mqtts_t *c)
{
    uint16_t id = ++c->next_packet_id;
    if (id == 0) id = c->next_packet_id = 1;   /* MQTT 0 reserved */
    return id;
}

/* Hold io_mutex across one read of len bytes (used during sync handshake,
 * before reader task starts). Polls until done or timeout. */
static int recv_full_locked(mqtts_t *c, uint8_t *buf, int want, uint32_t deadline_ms)
{
    int got = 0;
    while (got < want) {
        if (rtos_get_time() > deadline_ms) return -1;
        int r = c->ops->read(&c->tr, buf + got, want - got);
        if (r < 0) return -1;
        got += r;
    }
    return got;
}

/* Read one full MQTT packet synchronously (before reader task starts).
 * Used to wait for CONNACK / SUBACK during the initial handshake.
 * Returns packet type or <0. Buffer must be c->rx_buf. */
static int recv_packet_sync(mqtts_t *c, uint32_t timeout_ms)
{
    uint32_t deadline = rtos_get_time() + timeout_ms;
    uint8_t *buf = c->rx_buf;
    int cap = (int)c->cfg.rx_buf_size;

    /* fixed header byte */
    if (recv_full_locked(c, buf, 1, deadline) < 0) return -1;

    /* remaining length (1-4 bytes) */
    int multiplier = 1, rem_len = 0, rl_bytes = 0;
    while (1) {
        if (rl_bytes == 4) return -1;
        if (recv_full_locked(c, buf + 1 + rl_bytes, 1, deadline) < 0) return -1;
        unsigned char d = buf[1 + rl_bytes];
        rl_bytes++;
        rem_len += (d & 0x7F) * multiplier;
        if ((d & 0x80) == 0) break;
        multiplier *= 128;
    }
    int hdr = 1 + rl_bytes;
    if (hdr + rem_len > cap) return -1;

    if (rem_len > 0 && recv_full_locked(c, buf + hdr, rem_len, deadline) < 0) {
        return -1;
    }

    MQTTHeader h;
    h.byte = buf[0];
    return h.bits.type;
}

/* ─────────────────────────── reader task ─────────────────────────── */

/* MQTTTransport getfn — pulls bytes through io_mutex. */
static int reader_getfn(void *sck, unsigned char *buf, int n)
{
    mqtts_t *c = (mqtts_t *)sck;
    rtos_lock_mutex(&c->io_mutex);
    int r = c->ops->read(&c->tr, buf, (size_t)n);
    rtos_unlock_mutex(&c->io_mutex);
    return r;  /* <0 err, 0 again, >0 bytes */
}

/* Caller MUST hold io_mutex. Sends already-serialized bytes from any buffer. */
static int send_locked(mqtts_t *c, const uint8_t *buf, int len)
{
    int w = c->ops->write(&c->tr, buf, (size_t)len);
    if (w == len) {
        c->last_send_ms = rtos_get_time();
        c->stat_tx_packets++;
    }
    return (w == len) ? 0 : -1;
}

static void send_pingreq(mqtts_t *c)
{
    rtos_lock_mutex(&c->io_mutex);
    int n = MQTTSerialize_pingreq(c->tx_buf, c->cfg.tx_buf_size);
    if (n > 0 && send_locked(c, c->tx_buf, n) == 0) {
        c->last_pingreq_ms = rtos_get_time();
        /* Bumped to LOGW so production logs show keepalive cadence — the
         * silent LOGD made it impossible to tell if 'PINGRESP timeout'
         * was a server latency issue or a client send-side stall. */
        LOGW("PINGREQ");
    }
    rtos_unlock_mutex(&c->io_mutex);
}

static void send_puback(mqtts_t *c, uint16_t pkt_id)
{
    rtos_lock_mutex(&c->io_mutex);
    int n = MQTTSerialize_puback(c->tx_buf, c->cfg.tx_buf_size, pkt_id);
    if (n > 0) send_locked(c, c->tx_buf, n);
    rtos_unlock_mutex(&c->io_mutex);
}

static void dispatch_publish(mqtts_t *c, uint8_t *buf, int len)
{
    unsigned char dup, retained;
    int qos;
    unsigned short pkt_id = 0;
    MQTTString topic = MQTTString_initializer;
    unsigned char *payload = NULL;
    int payload_len = 0;

    if (MQTTDeserialize_publish(&dup, &qos, &retained, &pkt_id,
                                &topic, &payload, &payload_len, buf, len) != 1) {
        LOGE("PUBLISH deserialize failed");
        return;
    }

    if (qos == 1) send_puback(c, pkt_id);

    if (c->msg_cb) {
        const char *t_str = topic.lenstring.data ? topic.lenstring.data : topic.cstring;
        size_t       t_len = topic.lenstring.data ? (size_t)topic.lenstring.len
                                                  : (topic.cstring ? strlen(topic.cstring) : 0);
        c->msg_cb(c->msg_user, t_str, t_len, payload, (size_t)payload_len);
    }
}

static void dispatch_packet(mqtts_t *c, int type, uint8_t *buf, int len)
{
    c->stat_rx_packets++;
    switch (type) {
    case PUBLISH:
        dispatch_publish(c, buf, len);
        break;
    case PUBACK: {
        unsigned char pkt_type, dup;
        unsigned short pkt_id = 0;
        if (MQTTDeserialize_ack(&pkt_type, &dup, &pkt_id, buf, len) == 1) {
            if (c->evt_cb) c->evt_cb(c->evt_user, MQTTS_EVT_PUBLISHED, pkt_id);
        }
        break;
    }
    case SUBACK: {
        unsigned short pkt_id = 0;
        int count = 0, granted[1] = {0};
        if (MQTTDeserialize_suback(&pkt_id, 1, &count, granted, buf, len) == 1) {
            LOGI("SUBACK id=%u granted=%d", pkt_id, count > 0 ? granted[0] : -1);
            if (c->evt_cb) c->evt_cb(c->evt_user, MQTTS_EVT_SUBSCRIBED, pkt_id);
        }
        break;
    }
    case UNSUBACK: {
        unsigned char pkt_type, dup;
        unsigned short pkt_id = 0;
        if (MQTTDeserialize_ack(&pkt_type, &dup, &pkt_id, buf, len) == 1) {
            LOGI("UNSUBACK id=%u", pkt_id);
        }
        break;
    }
    case PINGRESP: {
        uint32_t now = rtos_get_time();
        c->last_pingresp_ms = now;
        /* Log RTT so future field reports tell us whether the server's
         * actual PINGRESP latency stays inside our 30s window. */
        LOGW("PINGRESP rtt=%ums",
             (unsigned)(c->last_pingreq_ms ? (now - c->last_pingreq_ms) : 0));
        break;
    }
    default:
        LOGD("rx packet type=%d", type);
        break;
    }
}

static char *strdup_heap(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)os_malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* Send a SUBSCRIBE for one topic. Caller does NOT hold io_mutex. */
static int send_subscribe_locked(mqtts_t *c, const char *topic, int qos)
{
    MQTTString topic_str = MQTTString_initializer;
    topic_str.cstring = (char *)topic;
    int qos_arr[1] = { qos };
    uint16_t pkt_id = next_pid(c);

    rtos_lock_mutex(&c->io_mutex);
    int n = MQTTSerialize_subscribe(c->tx_buf, c->cfg.tx_buf_size, 0, pkt_id, 1, &topic_str, qos_arr);
    int rc = (n > 0) ? send_locked(c, c->tx_buf, n) : -1;
    rtos_unlock_mutex(&c->io_mutex);

    if (rc == 0) LOGI("SUB id=%u topic=%s qos=%d", pkt_id, topic, qos);
    else         LOGE("SUBSCRIBE failed (serialize n=%d)", n);
    return rc;
}

/* Send an UNSUBSCRIBE for one topic. Caller does NOT hold io_mutex. */
static int send_unsubscribe_locked(mqtts_t *c, const char *topic)
{
    MQTTString topic_str = MQTTString_initializer;
    topic_str.cstring = (char *)topic;
    uint16_t pkt_id = next_pid(c);

    rtos_lock_mutex(&c->io_mutex);
    int n = MQTTSerialize_unsubscribe(c->tx_buf, c->cfg.tx_buf_size, 0, pkt_id, 1, &topic_str);
    int rc = (n > 0) ? send_locked(c, c->tx_buf, n) : -1;
    rtos_unlock_mutex(&c->io_mutex);

    if (rc == 0) LOGI("UNSUB id=%u topic=%s", pkt_id, topic);
    else         LOGE("UNSUBSCRIBE failed (serialize n=%d)", n);
    return rc;
}

/* Remove topic from sub book. Returns true if found+removed. Uses swap-remove
 * to keep the book compact. */
static bool remove_from_sub_book(mqtts_t *c, const char *topic)
{
    for (uint8_t i = 0; i < c->sub_count; i++) {
        if (c->subs[i].topic && 0 == strcmp(c->subs[i].topic, topic)) {
            os_free(c->subs[i].topic);
            /* swap last entry into this slot */
            uint8_t last = c->sub_count - 1;
            if (i != last) {
                c->subs[i] = c->subs[last];
            }
            c->subs[last].topic = NULL;
            c->subs[last].qos   = 0;
            c->sub_count--;
            return true;
        }
    }
    return false;
}

/* Insert (or update qos of) topic in the sub book. Returns true on success. */
static bool add_to_sub_book(mqtts_t *c, const char *topic, int qos)
{
    /* Update existing */
    for (uint8_t i = 0; i < c->sub_count; i++) {
        if (c->subs[i].topic && 0 == strcmp(c->subs[i].topic, topic)) {
            c->subs[i].qos = (uint8_t)qos;
            return true;
        }
    }
    /* Append */
    if (c->sub_count >= c->cfg.max_subscriptions) {
        LOGE("sub book full (max=%u)", c->cfg.max_subscriptions);
        return false;
    }
    char *t_copy = strdup_heap(topic);
    if (!t_copy) return false;
    c->subs[c->sub_count].topic = t_copy;
    c->subs[c->sub_count].qos   = (uint8_t)qos;
    c->sub_count++;
    return true;
}

/* Replay every entry in sub book. Returns 0 on full success. */
static int replay_subscriptions(mqtts_t *c)
{
    for (uint8_t i = 0; i < c->sub_count; i++) {
        if (!c->subs[i].topic) continue;
        if (send_subscribe_locked(c, c->subs[i].topic, c->subs[i].qos) != 0) {
            LOGE("replay SUB[%u] failed", i);
            return -1;
        }
    }
    if (c->sub_count > 0) LOGW("replayed %u subscription(s)", c->sub_count);
    return 0;
}

/* Open transport (TLS or plain TCP) — wraps the existing tls_open / tcp_open.
 * For TLS, captures verify_result flags into stats. */
static int transport_open(mqtts_t *c)
{
    int rc;
    if (c->cfg.use_tls) {
        uint32_t flags = 0;
        rc = tls_open(&c->tr, c->host, c->cfg.port,
                      c->cfg.read_timeout_ms, c->cfg.connect_timeout_ms,
                      c->cfg.ca_pem, &flags);
        c->stat_last_verify_flags = flags;   /* stash even on success (may be tolerated flags) */
    } else {
        rc = tcp_open(&c->tr, c->host, c->cfg.port, c->cfg.read_timeout_ms);
        c->stat_last_verify_flags = 0;
    }
    if (rc != 0) {
        LOGE("transport open failed (use_tls=%d)", c->cfg.use_tls);
        return -1;
    }
    LOGW("transport=%s host=%s port=%u",
         c->cfg.use_tls ? "TLS" : "TCP", c->host, c->cfg.port);
    return 0;
}

/* Close socket + free TLS session manually so reconnect can rebuild from zero.
 * Skips ssl_close_notify entirely (would hang on dead WiFi). */
static void transport_close_socket(mqtts_t *c)
{
    if (c->cfg.use_tls) {
        if (c->tr.tls) {
            mbedtls_net_free(&c->tr.tls->server_fd);
            tls_destroy(c->tr.tls);
            c->tr.tls = NULL;
        }
        if (c->tr.cacert_inited) {
            mbedtls_x509_crt_free(&c->tr.cacert);
            c->tr.cacert_inited = false;
        }
    } else {
        mbedtls_net_free(&c->tr.tcp);
        if (c->tr.tcp_host) { os_free(c->tr.tcp_host); c->tr.tcp_host = NULL; }
        if (c->tr.tcp_port) { os_free(c->tr.tcp_port); c->tr.tcp_port = NULL; }
    }
}

/* Send CONNECT, wait for CONNACK. Caller MUST own io_mutex briefly here. */
static int send_connect_and_wait_connack(mqtts_t *c)
{
    MQTTPacket_connectData conn = MQTTPacket_connectData_initializer;
    conn.MQTTVersion = 4;  /* 3.1.1 */
    conn.clientID.cstring = c->client_id;
    conn.keepAliveInterval = c->cfg.keepalive_sec;
    conn.cleansession = 1;
    if (c->username) conn.username.cstring = c->username;
    if (c->password) conn.password.cstring = c->password;

    rtos_lock_mutex(&c->io_mutex);
    int n = MQTTSerialize_connect(c->tx_buf, c->cfg.tx_buf_size, &conn);
    int sent_ok = (n > 0) && (send_locked(c, c->tx_buf, n) == 0);
    int t = sent_ok ? recv_packet_sync(c, c->cfg.connect_timeout_ms) : -1;
    rtos_unlock_mutex(&c->io_mutex);

    if (!sent_ok) { LOGE("CONNECT send failed (n=%d)", n); return -1; }
    if (t != CONNACK) { LOGE("CONNACK not received (got type=%d)", t); return -1; }

    unsigned char session_present, connack_rc;
    if (MQTTDeserialize_connack(&session_present, &connack_rc,
                                c->rx_buf, (int)c->cfg.rx_buf_size) != 1
        || connack_rc != 0) {
        LOGE("CONNACK rc=%d", connack_rc);
        return -1;
    }
    LOGW("CONNACK rc=0");
    return 0;
}

/* Full bring-up: open transport, CONNECT/CONNACK, replay subscriptions. */
static int do_connect_and_replay(mqtts_t *c)
{
    if (transport_open(c) != 0) return -1;
    if (send_connect_and_wait_connack(c) != 0) {
        transport_close_socket(c);
        return -1;
    }
    /* Reset liveness counters on every fresh connect */
    c->last_pingreq_ms  = 0;
    c->last_pingresp_ms = 0;
    c->last_send_ms     = rtos_get_time();

    if (replay_subscriptions(c) != 0) {
        transport_close_socket(c);
        return -1;
    }
    return 0;
}

/* Sleep with ±50% jitter around cur_backoff_ms; disconnect can wake us via
 * backoff_wakeup_sem (returns kNoErr immediately on set). After sleep, double
 * cur_backoff_ms (capped at reconnect_max_ms). Doubling tracks the base, not
 * the jittered value, so the schedule still grows 1s→2s→4s→...→60s. */
static void backoff_sleep(mqtts_t *c)
{
    uint32_t base = c->cur_backoff_ms;
    /* Pick uniformly in [base/2, base*3/2]. rtos_get_time() % N is poor
     * randomness but adequate to break perfect synchrony across devices. */
    uint32_t jittered = base / 2 + (rtos_get_time() % (base + 1));
    LOGW("reconnecting in %ums (jittered from %ums)",
         (unsigned)jittered, (unsigned)base);

    /* Wait on backoff_wakeup_sem; mqtts_disconnect signals it for fast exit. */
    if (c->backoff_wakeup_sem) {
        rtos_get_semaphore(&c->backoff_wakeup_sem, jittered);
    } else {
        rtos_delay_milliseconds(jittered);
    }

    c->cur_backoff_ms = base * 2;
    if (c->cur_backoff_ms > c->cfg.reconnect_max_ms) {
        c->cur_backoff_ms = c->cfg.reconnect_max_ms;
    }
}

static bool pingresp_timed_out(const mqtts_t *c)
{
    if (c->cfg.pingresp_timeout_ms == 0) return false;
    if (c->last_pingreq_ms == 0)         return false;
    if (c->last_pingresp_ms >= c->last_pingreq_ms) return false;  /* answered */
    return (rtos_get_time() - c->last_pingreq_ms) > c->cfg.pingresp_timeout_ms;
}

static void reader_task(beken_thread_arg_t arg)
{
    mqtts_t *c = (mqtts_t *)arg;
    LOGI("reader task started");

    while (c->reader_run) {
        /* (re)connect attempt */
        int rc = do_connect_and_replay(c);
        if (rc == 0) {
            c->state = ST_CONNECTED;
            c->connected_since_ms = rtos_get_time();
            c->cur_backoff_ms = c->cfg.reconnect_min_ms;
            if (c->first_connect_done) {
                /* This is a reconnect, not the initial connect */
                c->stat_reconnect_count++;
            } else {
                c->first_connect_done = true;
                c->first_connect_result = 0;
                if (c->first_connect_sem) rtos_set_semaphore(&c->first_connect_sem);
            }
            if (c->evt_cb) c->evt_cb(c->evt_user, MQTTS_EVT_CONNECTED, 0);
        } else {
            /* connect failed */
            c->stat_connect_failures++;
            if (!c->first_connect_done) {
                c->first_connect_done = true;
                c->first_connect_result = -1;
                if (c->first_connect_sem) rtos_set_semaphore(&c->first_connect_sem);
                if (!c->cfg.auto_reconnect) break;
            }
            if (!c->cfg.auto_reconnect) break;
            backoff_sleep(c);
            continue;
        }

        /* read loop while connected */
        MQTTTransport trp = { .getfn = reader_getfn, .sck = c, .multiplier = 1,
                              .rem_len = 0, .len = 0, .state = 0 };
        int disc_reason = 0;
        while (c->reader_run) {
            int t = MQTTPacket_readnb(c->rx_buf, (int)c->cfg.rx_buf_size, &trp);
            if (t > 0) {
                dispatch_packet(c, t, c->rx_buf, trp.len);
            } else if (t < 0) {
                LOGE("reader: transport error");
                disc_reason = -1;
                break;
            } else {
                rtos_delay_milliseconds(c->cfg.read_timeout_ms);
            }

            uint32_t now = rtos_get_time();
            uint32_t interval = (uint32_t)c->cfg.keepalive_sec * 500;
            if (interval && (now - c->last_send_ms) >= interval) {
                send_pingreq(c);
            }
            if (pingresp_timed_out(c)) {
                LOGE("PINGRESP timeout (>%ums) — forcing reconnect",
                     (unsigned)c->cfg.pingresp_timeout_ms);
                c->stat_pingresp_misses++;
                disc_reason = -2;
                break;
            }
        }

        /* exited read loop: either user-requested disconnect or transport error */
        c->state = ST_RECONNECTING;
        c->connected_since_ms = 0;
        transport_close_socket(c);
        if (c->evt_cb) c->evt_cb(c->evt_user, MQTTS_EVT_DISCONNECTED, disc_reason);

        if (!c->reader_run) break;          /* user disconnect */
        if (!c->cfg.auto_reconnect) break;  /* caller opted out */
        backoff_sleep(c);
    }

    c->state = ST_IDLE;
    LOGI("reader task exiting");
    c->reader_thread = NULL;
    if (c->reader_exit_sem) rtos_set_semaphore(&c->reader_exit_sem);
    rtos_delete_thread(NULL);
}

/* ─────────────────────────── public API ─────────────────────────── */

mqtts_t *mqtts_create(const mqtts_config_t *cfg)
{
    if (!cfg || !cfg->host || !cfg->client_id) return NULL;

    mqtts_t *c = (mqtts_t *)os_zalloc(sizeof(mqtts_t));
    if (!c) return NULL;

    c->cfg = *cfg;
    if (c->cfg.keepalive_sec       == 0) c->cfg.keepalive_sec       = 60;
    if (c->cfg.connect_timeout_ms  == 0) c->cfg.connect_timeout_ms  = 10000;
    if (c->cfg.read_timeout_ms     == 0) c->cfg.read_timeout_ms     = 100;
    if (c->cfg.rx_buf_size         == 0) c->cfg.rx_buf_size         = 4096;
    if (c->cfg.tx_buf_size         == 0) c->cfg.tx_buf_size         = 2048;
    if (c->cfg.reader_stack_size   == 0) c->cfg.reader_stack_size   = 8192;
    if (c->cfg.reader_priority     == 0) c->cfg.reader_priority     = 4;
    if (c->cfg.reconnect_min_ms    == 0) c->cfg.reconnect_min_ms    = 1000;
    if (c->cfg.reconnect_max_ms    == 0) c->cfg.reconnect_max_ms    = 60000;
    if (c->cfg.pingresp_timeout_ms == 0 && cfg->pingresp_timeout_ms == 0) {
        c->cfg.pingresp_timeout_ms = 10000;  /* default on; user sets explicit 1 to nearly disable */
    }
    if (c->cfg.max_subscriptions   == 0) c->cfg.max_subscriptions   = 8;
    /* Note: bool auto_reconnect — caller's literal value is honored. To make
     * default true while still allowing explicit false, we require caller to
     * always set it explicitly. (Alternative would be a tri-state enum.) */

    c->host      = strdup_heap(cfg->host);
    c->client_id = strdup_heap(cfg->client_id);
    c->username  = strdup_heap(cfg->username);
    c->password  = strdup_heap(cfg->password);
    c->rx_buf    = (uint8_t *)psram_malloc(c->cfg.rx_buf_size);
    c->tx_buf    = (uint8_t *)psram_malloc(c->cfg.tx_buf_size);
    c->subs      = (mqtts_sub_entry_t *)os_zalloc(
                       sizeof(mqtts_sub_entry_t) * c->cfg.max_subscriptions);
    if (!c->host || !c->client_id || !c->rx_buf || !c->tx_buf || !c->subs) goto fail;

    if (rtos_init_mutex(&c->io_mutex) != kNoErr) goto fail;

    c->ops = c->cfg.use_tls ? &TLS_OPS : &TCP_OPS;
    c->next_packet_id = 0;
    c->state = ST_IDLE;
    return c;

fail:
    mqtts_destroy(c);
    return NULL;
}

void mqtts_destroy(mqtts_t *c)
{
    if (!c) return;
    mqtts_disconnect(c);  /* also calls transport_close_socket which frees TLS session */

    if (c->io_mutex) rtos_deinit_mutex(&c->io_mutex);

    /* Free subscription book */
    if (c->subs) {
        for (uint8_t i = 0; i < c->cfg.max_subscriptions; i++) {
            if (c->subs[i].topic) os_free(c->subs[i].topic);
        }
        os_free(c->subs);
    }

    if (c->rx_buf)    psram_free(c->rx_buf);
    if (c->tx_buf)    psram_free(c->tx_buf);
    if (c->host)      os_free(c->host);
    if (c->client_id) os_free(c->client_id);
    if (c->username)  os_free(c->username);
    if (c->password)  os_free(c->password);
    os_free(c);
}

void mqtts_get_stats(const mqtts_t *c, mqtts_stats_t *out)
{
    if (!c || !out) return;
    out->reconnect_count    = c->stat_reconnect_count;
    out->connect_failures   = c->stat_connect_failures;
    out->rx_packets         = c->stat_rx_packets;
    out->tx_packets         = c->stat_tx_packets;
    out->pingresp_misses    = c->stat_pingresp_misses;
    out->cur_backoff_ms     = (c->state == ST_CONNECTED) ? 0 : c->cur_backoff_ms;
    out->connected_since_ms = c->connected_since_ms;
    out->last_verify_flags  = c->stat_last_verify_flags;
}

void mqtts_set_message_cb(mqtts_t *c, mqtts_message_cb_t cb, void *user)
{
    if (!c) return;
    c->msg_cb = cb;
    c->msg_user = user;
}

void mqtts_set_event_cb(mqtts_t *c, mqtts_event_cb_t cb, void *user)
{
    if (!c) return;
    c->evt_cb = cb;
    c->evt_user = user;
}

bool mqtts_is_connected(const mqtts_t *c)
{
    return c && c->state == ST_CONNECTED;
}

int mqtts_connect(mqtts_t *c)
{
    if (!c) return -1;
    if (c->state == ST_CONNECTED) return 0;
    if (c->reader_thread) return 0;  /* already connecting in background */

    /* Prep semaphores fresh */
    if (c->reader_exit_sem)    { rtos_deinit_semaphore(&c->reader_exit_sem);    c->reader_exit_sem    = NULL; }
    if (c->first_connect_sem)  { rtos_deinit_semaphore(&c->first_connect_sem);  c->first_connect_sem  = NULL; }
    if (c->backoff_wakeup_sem) { rtos_deinit_semaphore(&c->backoff_wakeup_sem); c->backoff_wakeup_sem = NULL; }
    rtos_init_semaphore(&c->reader_exit_sem,    1);
    rtos_init_semaphore(&c->first_connect_sem,  1);
    rtos_init_semaphore(&c->backoff_wakeup_sem, 1);
    c->first_connect_result = -1;
    c->first_connect_done   = false;
    c->cur_backoff_ms = c->cfg.reconnect_min_ms;

    c->reader_run = true;
    bk_err_t err = rtos_create_thread(&c->reader_thread, c->cfg.reader_priority,
                                      "mqtts_rx", reader_task,
                                      c->cfg.reader_stack_size, (beken_thread_arg_t)c);
    if (err != kNoErr) {
        LOGE("reader thread create failed");
        c->reader_run = false;
        return -1;
    }

    /* Wait for first connect attempt to finish (success or timeout). After
     * this point we mark first_connect_done=true so reader will not touch
     * first_connect_sem again (avoids set-after-deinit race if caller times
     * out and proceeds to mqtts_disconnect while reader is still retrying). */
    rtos_get_semaphore(&c->first_connect_sem, c->cfg.connect_timeout_ms + 5000);
    c->first_connect_done = true;
    return c->first_connect_result;
}

int mqtts_disconnect(mqtts_t *c)
{
    if (!c) return -1;
    if (c->state == ST_IDLE && !c->reader_thread) return 0;

    c->state = ST_DISCONNECTING;
    c->reader_run = false;

    /* Wake reader if it's sleeping in backoff. Bounded wait: reader may still
     * be inside do_connect_and_replay (TLS handshake up to connect_timeout_ms)
     * which we cannot interrupt cleanly without yanking the socket. */
    if (c->backoff_wakeup_sem) rtos_set_semaphore(&c->backoff_wakeup_sem);

    if (c->reader_thread) {
        if (!c->reader_exit_sem) rtos_init_semaphore(&c->reader_exit_sem, 1);
        rtos_get_semaphore(&c->reader_exit_sem,
                           c->cfg.connect_timeout_ms + 2000);
    }

    /* Strict order: reader is now exited (or timed out). Safe to deinit sems. */
    if (c->reader_exit_sem)    { rtos_deinit_semaphore(&c->reader_exit_sem);    c->reader_exit_sem    = NULL; }
    if (c->first_connect_sem)  { rtos_deinit_semaphore(&c->first_connect_sem);  c->first_connect_sem  = NULL; }
    if (c->backoff_wakeup_sem) { rtos_deinit_semaphore(&c->backoff_wakeup_sem); c->backoff_wakeup_sem = NULL; }

    /* Close socket + free TLS session (reader's transport_close_socket may
     * have done it, but be safe in case reader exited mid-connect). */
    transport_close_socket(c);

    c->state = ST_IDLE;
    return 0;
}

int mqtts_subscribe(mqtts_t *c, const char *topic, int qos)
{
    if (!c || !topic) return -1;
    if (qos < 0 || qos > 1) qos = 1;

    /* Book first so reconnect can replay even if we're currently disconnected. */
    if (!add_to_sub_book(c, topic, qos)) return -1;

    /* Send now if connected; otherwise the reader will replay on next connect. */
    if (c->state == ST_CONNECTED) {
        return send_subscribe_locked(c, topic, qos);
    }
    return 0;
}

int mqtts_unsubscribe(mqtts_t *c, const char *topic)
{
    if (!c || !topic) return -1;

    /* Remove from book first so a concurrent reconnect won't re-subscribe it. */
    if (!remove_from_sub_book(c, topic)) {
        LOGW("unsubscribe: topic not in book: %s", topic);
        return -1;
    }

    /* Send UNSUBSCRIBE to broker if currently connected; if not, book removal
     * is enough — reconnect won't replay this topic. */
    if (c->state == ST_CONNECTED) {
        return send_unsubscribe_locked(c, topic);
    }
    return 0;
}

int mqtts_publish(mqtts_t *c, const char *topic,
                  const void *payload, size_t len, int qos)
{
    if (!c || !topic) return -1;
    if (qos < 0 || qos > 1) qos = 0;

    if (c->state != ST_CONNECTED) {
        if (c->evt_cb) {
            c->evt_cb(c->evt_user, MQTTS_EVT_PUBLISH_FAILED,
                      ((int)qos << 16) | 1 /* not-connected */);
        }
        return -1;
    }

    MQTTString topic_str = MQTTString_initializer;
    topic_str.cstring = (char *)topic;
    uint16_t pkt_id = (qos > 0) ? next_pid(c) : 0;

    rtos_lock_mutex(&c->io_mutex);
    int n = MQTTSerialize_publish(c->tx_buf, c->cfg.tx_buf_size,
                                  0, qos, 0, pkt_id,
                                  topic_str, (unsigned char *)payload, (int)len);
    int rc = (n > 0) ? send_locked(c, c->tx_buf, n) : -1;
    rtos_unlock_mutex(&c->io_mutex);

    if (rc != 0) {
        LOGE("PUBLISH failed (serialize n=%d, payload=%u, tx_buf=%u)",
             n, (unsigned)len, (unsigned)c->cfg.tx_buf_size);
        if (c->evt_cb) {
            c->evt_cb(c->evt_user, MQTTS_EVT_PUBLISH_FAILED,
                      ((int)qos << 16) | 2 /* serialize/send failed */);
        }
    }
    return rc;
}
