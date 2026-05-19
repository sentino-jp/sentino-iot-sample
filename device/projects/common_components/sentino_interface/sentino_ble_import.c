#include <stddef.h>
#include <os/os.h>                      /* beken_thread_t — app_event.h needs it */

#include "sentino_ble_import.h"
#include "sentino_ble_proto.h"          /* SDK */
#include "sentino_ble_v1.h"             /* V1_STATUS_* */
#include "sentino_provision_import.h"   /* adapter — provision persistence */
#include "sentino_mqtt_import.h"        /* adapter — Register_Sentino_Cloud_Ready_Cb */
#include "sentino_mqtt.h"               /* sentino_provision_set_bind */
#include "app_event.h"                  /* APP_EVT_NETWORK_PROVISIONING_SUCCESS */

static sentino_ble_wifi_connect_fn_t s_wifi_connect = NULL;
static sentino_ble_wifi_scan_fn_t    s_wifi_scan    = NULL;

/* ── proto ops impl ─────────────────────────────────────────────── */

static void on_network_set(const char *sid,      const char *pw,
                           const char *user_id,  const char *asset_id,
                           const char *mqtt_url,
                           uint16_t port, uint16_t ssl_port)
{
    /* Sentino side: persist user/asset/broker/ports to NVS for the engine
     * to pick up on the next sentino_iot_engine_init (triggered after
     * WiFi-up). Both ports forwarded as-received; engine decides which to
     * use by field-presence. */
    sentino_provision_apply_from_ble(user_id, asset_id, mqtt_url, port, ssl_port);

    /* BK side: kick off WiFi STA connect. BSP owns BK boarding op codes. */
    if (s_wifi_connect) s_wifi_connect(sid, pw);
}

static void on_wifi_scan_request(void)
{
    if (s_wifi_scan) s_wifi_scan();
}

/* WiFi-up signal during active provisioning. bk_smart_config posts
 * APP_EVT_NETWORK_PROVISIONING_SUCCESS only inside the smart_config_running
 * branch (fresh provision via BLE), not on plain reconnect — exactly when
 * the phone is still on the BLE link to receive this. */
static void on_wifi_provisioned(app_evt_msg_t *msg, void *user_data)
{
    (void)msg; (void)user_data;
    sentino_ble_proto_emit_status(V1_STATUS_WIFI_CONNECTED);
}

/* MQTT-up signal — fires once per CONNECTED (boot + every reconnect). The
 * phone may have moved on (Web stays connected to capture this; Flutter
 * disconnects after thing.network.set so the indicate is a noop there). */
static void on_cloud_ready(void)
{
    sentino_ble_proto_emit_status(V1_STATUS_MQTT_CONNECTED);
}

/* Cloud bind ack — res==0 means cloud accepted bind. Persist to NVS so
 * subsequent device.information.get reports the real bind state, and
 * push 1801 to the phone. Non-zero responses leave bind_state untouched
 * and the phone on its REST polling fallback. */
static void on_bind_ack(int res)
{
    if (res == 0) {
        sentino_provision_set_bind(true);
        sentino_ble_proto_emit_status(V1_STATUS_BIND_SUCCESS);
    }
}

/* ── BSP-facing API ────────────────────────────────────────────── */

void sentino_ble_init(sentino_ble_indicate_fn_t     indicate_fn,
                      sentino_ble_wifi_connect_fn_t wifi_connect_fn,
                      sentino_ble_wifi_scan_fn_t    wifi_scan_fn)
{
    s_wifi_connect = wifi_connect_fn;
    s_wifi_scan    = wifi_scan_fn;

    sentino_ble_proto_ops_t ops = {
        .send_indicate        = indicate_fn,
        .on_network_set       = on_network_set,
        .on_wifi_scan_request = on_wifi_scan_request,
    };
    sentino_ble_proto_init(&ops);

    /* 1006 = WIFI_CONNECTED via app_event provisioning-success hook.
     * 1703 = MQTT_CONNECTED via cloud-ready hook (engine fires after
     * bind+info pushed on a fresh CONNECTED).
     * 1801 = BIND_SUCCESS via mqtt bind-ack hook (cloud responds to our
     * publish_bind with code=bind, res=0). */
    app_event_register_handler(APP_EVT_NETWORK_PROVISIONING_SUCCESS,
                               on_wifi_provisioned, NULL);
    Register_Sentino_Cloud_Ready_Cb(on_cloud_ready);
    Register_Sentino_Bind_Ack_Cb(on_bind_ack);
}

void sentino_ble_deinit(void)
{
    sentino_ble_proto_deinit();
    s_wifi_connect = NULL;
    s_wifi_scan    = NULL;
}

void sentino_ble_on_ble_connect(void)    { sentino_ble_proto_on_connect();    }
void sentino_ble_on_ble_disconnect(void) { sentino_ble_proto_on_disconnect(); }

void sentino_ble_on_ble_write(const uint8_t *bytes, uint16_t len)
{
    sentino_ble_proto_feed(bytes, len);
}

void sentino_ble_on_wifi_scan_done(const char * const *ssids, int count)
{
    sentino_ble_proto_emit_wifi_list(ssids, count);
}
