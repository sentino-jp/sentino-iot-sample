/*
 * mqtts — generic MQTT 3.1.1 client for IoT firmware.
 *
 * Despite the name "mqtts", this client supports BOTH plain TCP (mqtt://) and
 * TLS (mqtts://) — pick via cfg.use_tls.
 *
 * SECURITY: TLS path is currently VERIFY_NONE — server cert is NOT validated.
 * Traffic is encrypted but susceptible to MITM. Real CA verification is a
 * separate work item (requires bypassing tls_connect.c).
 *
 * Layering:
 *   - TLS path:   mbedtls_client_* (psa_mbedtls/tls_connect.h)
 *   - Plain TCP:  mbedtls_net_* primitives
 *   - Protocol:   MQTTPacket serialize/readnb (ali_mqtt sublib)
 *
 * No business logic, no vendor-specific calls. One reader task per client,
 * one io_mutex serializes read/write. Reader owns reconnect: on transport
 * error it sleeps backoff (with jitter) then retries CONNECT and replays
 * subscriptions. Caller is notified via MQTTS_EVT_CONNECTED/DISCONNECTED.
 */

#ifndef __MQTTS_H__
#define __MQTTS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct mqtts mqtts_t;

typedef enum {
    MQTTS_EVT_CONNECTED,        /* arg = 0 */
    MQTTS_EVT_DISCONNECTED,     /* arg: 0 = caller-initiated, <0 = transport/protocol error */
    MQTTS_EVT_SUBSCRIBED,       /* arg = packet id of the SUBSCRIBE that was acked */
    MQTTS_EVT_PUBLISHED,        /* arg = packet id (QoS 1 PUBACK arrived) */
    MQTTS_EVT_PUBLISH_FAILED,   /* arg = (qos << 16) | (uint16_t)(-rc); fired
                                 * synchronously from mqtts_publish on failure */
} mqtts_event_t;

/* Inbound PUBLISH delivery. topic and payload are NOT null-terminated.
 * Buffers are owned by the client; copy if you need them past the callback. */
typedef void (*mqtts_message_cb_t)(void *user,
                                   const char *topic, size_t topic_len,
                                   const uint8_t *payload, size_t payload_len);

typedef void (*mqtts_event_cb_t)(void *user, mqtts_event_t evt, int arg);

typedef struct {
    /* Connection */
    const char *host;
    uint16_t    port;

    /* MQTT identity */
    const char *client_id;
    const char *username;           /* NULL = anonymous */
    const char *password;           /* NULL = anonymous */

    /* Timing */
    uint16_t keepalive_sec;         /* default 60 */
    uint16_t connect_timeout_ms;    /* default 10000 — TLS handshake + CONNACK */
    uint16_t read_timeout_ms;       /* default 100 — per-read timeout in reader loop */

    /* Transport */
    bool        use_tls;            /* false = plain TCP, true = TLS via mbedtls */
    const char *ca_pem;             /* PEM string, NULL-terminated. TLS path only.
                                     *   NULL  → MBEDTLS_SSL_VERIFY_NONE (legacy/dev)
                                     *   !NULL → VERIFY_OPTIONAL with selective reject:
                                     *           chain/hostname/usage failures abort,
                                     *           time-validity failures (FUTURE/EXPIRED)
                                     *           are tolerated and logged. The time
                                     *           tolerance will be removed in a follow-up
                                     *           after NTP sync lands (then equivalent
                                     *           to VERIFY_REQUIRED). */

    /* Buffers — heap-allocated by client */
    size_t rx_buf_size;             /* default 4096 */
    size_t tx_buf_size;             /* default 2048 */

    /* Reader task */
    uint32_t reader_stack_size;     /* default 8192 (cJSON in cb path) */
    uint8_t  reader_priority;       /* default 4 */

    /* Auto-reconnect with exponential backoff. Reader task stays alive across
     * disconnects; on transport error it sleeps backoff then retries CONNECT. */
    bool     auto_reconnect;        /* default true */
    uint32_t reconnect_min_ms;      /* default 1000  — initial backoff */
    uint32_t reconnect_max_ms;      /* default 60000 — cap, doubles each fail */

    /* Server-liveness check. After PINGREQ, if PINGRESP doesn't arrive within
     * this window, force disconnect → triggers reconnect. 0 = disabled. */
    uint32_t pingresp_timeout_ms;   /* default 10000 */

    /* Subscription book — replayed automatically after reconnect. */
    uint8_t  max_subscriptions;     /* default 8 */
} mqtts_config_t;

/* Convenience initializer with all sensible defaults. Caller still must set
 * the required fields (host / client_id / use_tls / etc.) afterwards.
 *
 *   mqtts_config_t cfg = MQTTS_CFG_DEFAULTS();
 *   cfg.host      = "broker.example.com";
 *   cfg.port      = 8883;
 *   cfg.client_id = "device-42";
 *   cfg.use_tls   = true;
 */
/* Defaults tuned for BK7258 + WiFi + 8883 TLS to mqtt-iot.sentino.jp.
 * Earlier 10s timeouts were too tight for this combo — server PINGRESP
 * latency under TLS regularly hit the 10-30s range, causing repeated
 * spurious reconnects every ~40s of idle. Reference firmware (mi_mqtt)
 * uses 30s pingresp timeout; we match that. Handshake recv was also
 * timing out at 10s on slow cert-chain fragments. */
#define MQTTS_CFG_DEFAULTS() ((mqtts_config_t){ \
    .keepalive_sec       = 60,    \
    .connect_timeout_ms  = 15000, \
    .read_timeout_ms     = 100,   \
    .rx_buf_size         = 4096,  \
    .tx_buf_size         = 2048,  \
    .reader_stack_size   = 8192,  \
    .reader_priority     = 4,     \
    .auto_reconnect      = true,  \
    .reconnect_min_ms    = 1000,  \
    .reconnect_max_ms    = 60000, \
    .pingresp_timeout_ms = 30000, \
    .max_subscriptions   = 8,     \
})

/* Runtime stats — useful for production health dashboards / diag CLI. */
typedef struct {
    uint32_t reconnect_count;     /* successful reconnects (excludes initial connect) */
    uint32_t connect_failures;    /* failed connect attempts (each backoff iteration) */
    uint32_t rx_packets;          /* total inbound packets dispatched */
    uint32_t tx_packets;          /* total outbound packets sent OK */
    uint32_t pingresp_misses;     /* PINGRESP timeouts that forced a disconnect */
    uint32_t cur_backoff_ms;      /* current backoff value; 0 when connected */
    uint32_t connected_since_ms;  /* rtos_get_time() value at last successful connect, 0 if not connected */
    uint32_t last_verify_flags;   /* mbedtls_ssl_get_verify_result of last TLS handshake;
                                   * 0 means cert fully verified. Common non-fatal value:
                                   * 0x200 (BADCERT_FUTURE) when device clock < cert notBefore. */
} mqtts_stats_t;

/* Lifecycle */
mqtts_t *mqtts_create(const mqtts_config_t *cfg);
void     mqtts_destroy(mqtts_t *c);

/* Snapshot stats. Safe to call from any thread (32-bit reads atomic on ARM;
 * fields may be slightly inconsistent under race but adequate for monitoring). */
void mqtts_get_stats(const mqtts_t *c, mqtts_stats_t *out);

/* Callbacks (set before connect; safe to call again later) */
void mqtts_set_message_cb(mqtts_t *c, mqtts_message_cb_t cb, void *user);
void mqtts_set_event_cb  (mqtts_t *c, mqtts_event_cb_t   cb, void *user);

/* Connect: starts reader task, which opens transport (TLS handshake or TCP),
 * sends CONNECT, waits CONNACK. Returns 0 once CONNACK rc=0 is observed (or
 * on timeout/failure if auto_reconnect is off). With auto_reconnect=true the
 * call blocks until the first successful connect; subsequent reconnects are
 * handled inside reader_task without caller involvement. */
int  mqtts_connect(mqtts_t *c);

/* Disconnect: stops reader (also unblocks any backoff sleep), closes socket.
 * Does NOT send TLS close_notify (would hang if WiFi already down — same
 * lesson as IOT_MQTT_Destroy). */
int  mqtts_disconnect(mqtts_t *c);

bool mqtts_is_connected(const mqtts_t *c);

/* qos: 0 or 1. Returns 0 on success, negative on alloc/serialize failure.
 *
 * Disconnected behaviour: topic is added to the subscription book and will
 * be SUBSCRIBE'd automatically after the next reconnect. Returns 0 in that
 * case (book registration succeeded — the wire SUBSCRIBE happens later). */
int  mqtts_subscribe(mqtts_t *c, const char *topic, int qos);

/* Remove topic from subscription book and (if currently connected) send
 * UNSUBSCRIBE to broker. Disconnected: book-only removal, no packet sent.
 * Returns 0 on success, -1 if topic not in book or serialize/send failed. */
int  mqtts_unsubscribe(mqtts_t *c, const char *topic);

/* qos: 0 or 1. QoS 1 does NOT block for PUBACK — PUBACK arrival emits
 * MQTTS_EVT_PUBLISHED with packet id. No retransmit on loss.
 *
 * Disconnected behaviour: returns -1 AND synchronously emits
 * MQTTS_EVT_PUBLISH_FAILED so the caller can decide retry policy. Unlike
 * subscribes, publishes are NOT queued for replay. */
int  mqtts_publish  (mqtts_t *c, const char *topic,
                     const void *payload, size_t len, int qos);

/* ─────────────────────── Callback re-entrancy rules ───────────────────────
 * mqtts_message_cb_t and mqtts_event_cb_t run on the reader thread (or, for
 * MQTTS_EVT_PUBLISH_FAILED, on the publisher's calling thread).
 *
 *   MAY call from within callback:
 *     mqtts_subscribe / mqtts_unsubscribe / mqtts_publish / mqtts_is_connected
 *     / mqtts_get_stats
 *
 *   MUST NOT call from within callback:
 *     mqtts_disconnect / mqtts_destroy   (would deadlock waiting for self)
 * ─────────────────────────────────────────────────────────────────────── */

#ifdef __cplusplus
}
#endif
#endif /* __MQTTS_H__ */
