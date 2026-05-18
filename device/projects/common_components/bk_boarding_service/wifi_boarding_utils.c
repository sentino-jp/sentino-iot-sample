#include <common/sys_config.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <components/log.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>

#include "components/bluetooth/bk_dm_bluetooth.h"
#include "components/bluetooth/bk_dm_bluetooth_types.h"
#include "components/bluetooth/bk_dm_gap_ble_types.h"
#include "components/bluetooth/bk_dm_gap_ble.h"
#include "components/bluetooth/bk_dm_gatt_types.h"
#include "components/bluetooth/bk_dm_gatts.h"
#include "components/bk_uid.h"
#if CONFIG_SENTINO_IOT
/* ADV/scan_rsp still embed PID + UUID from the device triple — those are
 * identity, not protocol. The V1 protocol handler itself lives in
 * sentino_iot_sdk/sentino_ble/ and is reached via sentino_ble_import. */
#include "sentino_provision_import.h"
#include "sentino_ble_import.h"
#endif

#include "wifi_boarding_internal.h"
#include "wifi_boarding_utils.h"


static ble_boarding_info_t *s_ble_boarding_info = NULL;
static beken_semaphore_t s_ble_sema = NULL;
static bk_gatt_if_t s_gatts_if = 0;
extern bool enable_ble_split_pkt;

#define SYNC_CMD_TIMEOUT_MS 4000
#define ADV_HANDLE 0

/* Sentino Rlink BLE V1: GATT service 0x1910 with 0x2B11 (write) and 0x2B10 (notify) */


#define BK_GATT_ATTR_TYPE(iuuid) {.len = BK_UUID_LEN_16, .uuid = {.uuid16 = iuuid}}
#define BK_GATT_ATTR_CONTENT(iuuid) {.len = BK_UUID_LEN_16, .uuid = {.uuid16 = iuuid}}
#define BK_GATT_ATTR_VALUE(ilen, ivalue) {.attr_max_len = ilen, .attr_len = ilen, .attr_value = ivalue}

#define BK_GATT_ATTR_TYPE_128(iuuid) {.len = BK_UUID_LEN_128, .uuid = {.uuid128 = {iuuid[0], iuuid[1], iuuid[2], iuuid[3], iuuid[4], \
                                                                                       iuuid[5], iuuid[6], iuuid[7], iuuid[8], iuuid[9], iuuid[10], iuuid[11], iuuid[12], iuuid[13], iuuid[14], iuuid[15]}}}

#define BK_GATT_ATTR_CONTENT_128(iuuid) {.len = BK_UUID_LEN_128, .uuid = {.uuid128 = {iuuid[0], iuuid[1], iuuid[2], iuuid[3], iuuid[4], \
                                                                                          iuuid[5], iuuid[6], iuuid[7], iuuid[8], iuuid[9], iuuid[10], iuuid[11], iuuid[12], iuuid[13], iuuid[14], iuuid[15]}}}

#define BK_GATT_PRIMARY_SERVICE_DECL(iuuid) \
    .att_desc =\
               {\
                .attr_type = BK_GATT_ATTR_TYPE(BK_GATT_UUID_PRI_SERVICE),\
                .attr_content = BK_GATT_ATTR_CONTENT(iuuid),\
               }

#define BK_GATT_PRIMARY_SERVICE_DECL_128(iuuid) \
    .att_desc =\
               {\
                .attr_type = BK_GATT_ATTR_TYPE(BK_GATT_UUID_PRI_SERVICE),\
                .attr_content = BK_GATT_ATTR_CONTENT_128(iuuid)\
               }

#define BK_GATT_CHAR_DECL(iuuid, ilen, ivalue, iprop, iperm, irsp) \
    .att_desc = \
                {\
                 .attr_type = BK_GATT_ATTR_TYPE(BK_GATT_UUID_CHAR_DECLARE),\
                 .attr_content = BK_GATT_ATTR_CONTENT(iuuid),\
                 .value = BK_GATT_ATTR_VALUE(ilen, ivalue),\
                 .prop = iprop,\
                 .perm = iperm,\
                },\
                .attr_control = {.auto_rsp = irsp}

#define BK_GATT_CHAR_DECL_128(iuuid, ilen, ivalue, iprop, iperm, irsp) \
    .att_desc = \
                {\
                 .attr_type = BK_GATT_ATTR_TYPE(BK_GATT_UUID_CHAR_DECLARE),\
                 .attr_content = BK_GATT_ATTR_CONTENT_128(iuuid),\
                 .value = BK_GATT_ATTR_VALUE(ilen, ivalue),\
                 .prop = iprop,\
                 .perm = iperm,\
                },\
                .attr_control = {.auto_rsp = irsp}

#define BK_GATT_CHAR_DESC_DECL(iuuid, ilen, ivalue, iperm, irsp) \
    .att_desc = \
                {\
                 .attr_type = BK_GATT_ATTR_TYPE(iuuid),\
                 .value = BK_GATT_ATTR_VALUE(ilen, ivalue),\
                 .perm = iperm,\
                },\
                .attr_control = {.auto_rsp = irsp}

#define BK_GATT_CHAR_DESC_DECL_128(iuuid, ilen, ivalue, iperm, irsp) \
    .att_desc = \
                {\
                 .attr_type = BK_GATT_ATTR_TYPE_128(iuuid),\
                 .value = BK_GATT_ATTR_VALUE(ilen, ivalue),\
                 .perm = iperm,\
                },\
                .attr_control = {.auto_rsp = irsp}

#define INVALID_ATTR_HANDLE 0

static uint16_t s_prop_cli_config;
static uint16_t s_conn_ind = ~0;

/* Sentino: GATT service 0x1910 with just write(0x2B11) + notify(0x2B10) */
static const bk_gatts_attr_db_t s_gatts_attr_db_service_boarding[] =
{
    /* Service: 0x1910 */
    {
        BK_GATT_PRIMARY_SERVICE_DECL(0x1910),
    },
    /* Notify characteristic: 0x2B10 (Device → App) */
    {
        BK_GATT_CHAR_DECL(0x2B10,
                          0, NULL,
                          BK_GATT_CHAR_PROP_BIT_NOTIFY | BK_GATT_CHAR_PROP_BIT_INDICATE,
                          BK_GATT_PERM_READ,
                          BK_GATT_RSP_BY_APP),
    },
    {
        BK_GATT_CHAR_DESC_DECL(BK_GATT_UUID_CHAR_CLIENT_CONFIG,
                               sizeof(s_prop_cli_config), (uint8_t *)&s_prop_cli_config,
                               BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                               BK_GATT_RSP_BY_APP),
    },
    /* Write characteristic: 0x2B11 (App → Device) */
    {
        BK_GATT_CHAR_DECL(0x2B11,
                          0, NULL,
                          BK_GATT_CHAR_PROP_BIT_WRITE | BK_GATT_CHAR_PROP_BIT_WRITE_NR,
                          BK_GATT_PERM_WRITE,
                          BK_GATT_RSP_BY_APP),
    },
};

static uint16_t s_service_attr_handle = INVALID_ATTR_HANDLE;
static uint16_t s_char_attr_handle = INVALID_ATTR_HANDLE;        /* notify char */
static uint16_t s_char_desc_attr_handle = INVALID_ATTR_HANDLE;   /* notify desc (CCCD) */

static uint16_t s_char_write_char_handle = INVALID_ATTR_HANDLE;  /* V1 write char (0x2B11) */

static uint16_t *const s_boarding_attr_handle_list[sizeof(s_gatts_attr_db_service_boarding) / sizeof(s_gatts_attr_db_service_boarding[0])] =
{
    &s_service_attr_handle,
    &s_char_attr_handle,
    &s_char_desc_attr_handle,
    &s_char_write_char_handle,
};

static int32_t dm_gatts_get_buff_from_attr_handle(bk_gatts_attr_db_t *attr_list, uint16_t *attr_handle_list, uint32_t size, uint16_t attr_handle, uint32_t *output_index, uint8_t **output_buff, uint32_t *output_size)
{
    uint32_t i;

    for (i = 0; i < size; ++i)
    {
        if (attr_handle_list[i] == attr_handle)
        {
            break;
        }
    }

    if (i >= size)
    {
        return -1;
    }

    *output_index = i;
    *output_buff = attr_list[i].att_desc.value.attr_value;
    *output_size = attr_list[i].att_desc.value.attr_len;

    return 0;
}

/* BSP-side fn pointers handed to the adapter at init. Adapter calls these
 * back when the phone sends thing.network.set / thing.network.getwifis. */
#if CONFIG_SENTINO_IOT
static void bsp_indicate(const uint8_t *data, uint16_t len)
{
    if (s_conn_ind == (uint16_t)~0) {
        wboard_loge("BLE not connected, can not indicate");
        return;
    }
    bk_ble_gatts_send_indicate(s_gatts_if, s_conn_ind, s_char_attr_handle,
                               len, (uint8_t *)data, 0);
}

static void bsp_wifi_connect(const char *sid, const char *pw)
{
    if (!s_ble_boarding_info) return;
    if (s_ble_boarding_info->ssid_value) os_free(s_ble_boarding_info->ssid_value);
    s_ble_boarding_info->ssid_value  = os_strdup(sid ? sid : "");
    s_ble_boarding_info->ssid_length = strlen(s_ble_boarding_info->ssid_value);
    if (s_ble_boarding_info->password_value) os_free(s_ble_boarding_info->password_value);
    s_ble_boarding_info->password_value  = os_strdup(pw ? pw : "");
    s_ble_boarding_info->password_length = strlen(s_ble_boarding_info->password_value);

    if (s_ble_boarding_info->cb) {
        /* msg.param=0 → boarding_core's BOARDING_OP_STATION_START handler
         * will read ssid/password we just populated and start WiFi STA. */
        s_ble_boarding_info->cb(BOARDING_OP_STATION_START, 0, NULL);
    }
}

static void bsp_wifi_scan(void)
{
    if (s_ble_boarding_info && s_ble_boarding_info->cb) {
        s_ble_boarding_info->cb(BOARDING_OP_START_WIFI_SCAN, 0, NULL);
    }
}
#endif /* CONFIG_SENTINO_IOT */

static int32_t wifi_boarding_gatts_cb(bk_gatts_cb_event_t event, bk_gatt_if_t gatts_if, bk_ble_gatts_cb_param_t *comm_param)
{
    ble_err_t ret = 0;

    switch (event)
    {
    case BK_GATTS_REG_EVT:
    {
        struct gatts_reg_evt_param *param = (typeof(param))comm_param;

        wboard_logi("BK_GATTS_REG_EVT %d %d", param->status, param->gatt_if);
        s_gatts_if = param->gatt_if;

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore(&s_ble_sema);
        }
    }
    break;

    case BK_GATTS_UNREG_EVT:
    {
        wboard_logi("BK_GATTS_UNREG_EVT");

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore( &s_ble_sema );
        }
    }
    break;

    case BK_GATTS_START_EVT:
    {
        struct gatts_start_evt_param *param = (typeof(param))comm_param;
        wboard_logi("BK_GATTS_START_EVT compl %d %d", param->status, param->service_handle);

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore(&s_ble_sema);
        }
    }
    break;

    case BK_GATTS_STOP_EVT:
    {
        struct gatts_stop_evt_param *param = (typeof(param))comm_param;
        wboard_logi("BK_GATTS_STOP_EVT compl %d %d", param->status, param->service_handle);

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore(&s_ble_sema);
        }
    }
    break;

    case BK_GATTS_CREAT_ATTR_TAB_EVT:
    {
        struct gatts_add_attr_tab_evt_param *param = (typeof(param))comm_param;

        wboard_logi("BK_GATTS_CREAT_ATTR_TAB_EVT %d %d", param->status, param->num_handle);

        for (int i = 0; i < param->num_handle; ++i)
        {
            *s_boarding_attr_handle_list[i] = param->handles[i];
        }

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore(&s_ble_sema);
        }
    }
    break;

    case BK_GATTS_READ_EVT:
    {
        struct gatts_read_evt_param *param = (typeof(param))comm_param;
        wboard_logi("read attr handle %d need rsp %d", param->handle, param->need_rsp);

        /* Sentino V1: no readable characteristics, respond with empty */
        if (param->need_rsp) {
            bk_gatt_rsp_t rsp;
            memset(&rsp, 0, sizeof(rsp));
            rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
            rsp.attr_value.handle = param->handle;
            rsp.attr_value.len = 0;
            bk_ble_gatts_send_response(gatts_if, param->conn_id, param->trans_id, BK_GATT_OK, &rsp);
        }
    }
    break;

    case BK_GATTS_WRITE_EVT:
    {
        struct gatts_write_evt_param *param = (typeof(param))comm_param;

        wboard_logi("write attr handle %d len %d offset %d need rsp %d", param->handle, param->len, param->offset, param->need_rsp);


        /* Sentino V1: all writes go to 0x2B11, feed into packet assembler */
        if (s_char_write_char_handle == param->handle) {
            if (param->need_rsp) {
                bk_gatt_rsp_t wr_rsp;
                memset(&wr_rsp, 0, sizeof(wr_rsp));
                wr_rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
                wr_rsp.attr_value.handle = param->handle;
                wr_rsp.attr_value.offset = 0;
                wr_rsp.attr_value.len = 0;
                wr_rsp.attr_value.value = NULL;
                bk_ble_gatts_send_response(gatts_if, param->conn_id, param->trans_id, BK_GATT_OK, &wr_rsp);
            }

#if CONFIG_SENTINO_IOT
            sentino_ble_on_ble_write(param->value, param->len);
#endif
            break;
        } else if (s_char_desc_attr_handle == param->handle) {
            /* CCCD write — enable notifications */
            if (param->need_rsp) {
                bk_gatt_rsp_t cccd_rsp;
                memset(&cccd_rsp, 0, sizeof(cccd_rsp));
                cccd_rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
                cccd_rsp.attr_value.handle = param->handle;
                cccd_rsp.attr_value.offset = 0;
                cccd_rsp.attr_value.len = 0;
                bk_ble_gatts_send_response(gatts_if, param->conn_id, param->trans_id, BK_GATT_OK, &cccd_rsp);
            }
            break;
        }
        break; /* Sentino: no other characteristics to handle */

    }
    break;

    case BK_GATTS_EXEC_WRITE_EVT:
    {
        struct gatts_exec_write_evt_param *param = (typeof(param))comm_param;
        wboard_logi("exec write");
    }
    break;

    case BK_GATTS_CONF_EVT:
    {
        struct gatts_conf_evt_param *param = (typeof(param))comm_param;

        wboard_logi("BK_GATTS_CONF_EVT %d %d %d", param->status, param->conn_id, param->handle);
    }
    break;

    case BK_GATTS_RESPONSE_EVT:
    {
        struct gatts_rsp_evt_param *param = (typeof(param))comm_param;

        wboard_logi("BK_GATTS_RESPONSE_EVT %d %d", param->status, param->handle);
    }
    break;

    case BK_GATTS_SEND_SERVICE_CHANGE_EVT:
    {
        struct gatts_send_service_change_evt_param *param = (typeof(param))comm_param;

        wboard_logi("BK_GATTS_SEND_SERVICE_CHANGE_EVT %02x:%02x:%02x:%02x:%02x:%02x %d %d",
                    param->remote_bda[5],
                    param->remote_bda[4],
                    param->remote_bda[3],
                    param->remote_bda[2],
                    param->remote_bda[1],
                    param->remote_bda[0],
                    param->status, param->conn_id);
    }
    break;

    case BK_GATTS_CONNECT_EVT:
    {
        struct gatts_connect_evt_param *param = (typeof(param))comm_param;

        wboard_logi("BK_GATTS_CONNECT_EVT role %d %02X:%02X:%02X:%02X:%02X:%02X conn_id %d ",
                    param->link_role,
                    param->remote_bda[5],
                    param->remote_bda[4],
                    param->remote_bda[3],
                    param->remote_bda[2],
                    param->remote_bda[1],
                    param->remote_bda[0],
                    param->conn_id);

        s_conn_ind = param->conn_id;
#if CONFIG_SENTINO_IOT
        sentino_ble_on_ble_connect();
#endif
    }
    break;

    case BK_GATTS_DISCONNECT_EVT:
    {
        struct gatts_disconnect_evt_param *param = (typeof(param))comm_param;

        wboard_logi("BK_GATTS_DISCONNECT_EVT %02X:%02X:%02X:%02X:%02X:%02X conn_id %d",
                    param->remote_bda[5],
                    param->remote_bda[4],
                    param->remote_bda[3],
                    param->remote_bda[2],
                    param->remote_bda[1],
                    param->remote_bda[0],
                    param->conn_id
                   );

        s_conn_ind = ~0;
#if CONFIG_SENTINO_IOT
        sentino_ble_on_ble_disconnect();
#endif
    }
    break;

    case BK_GATTS_MTU_EVT:
    {
        struct gatts_mtu_evt_param *param = (typeof(param))comm_param;

        wboard_logi("BK_GATTS_MTU_EVT %d %d", param->conn_id, param->mtu);
    }
    break;

    default:
        break;
    }

    return ret;
}


static void dm_ble_gap_common_cb(bk_ble_gap_cb_event_t event, bk_ble_gap_cb_param_t *param)
{
    wboard_logd("event %d", event);

    switch (event)
    {
    case BK_BLE_GAP_CONNECT_COMPLETE_EVT:
    {
        struct ble_connect_complete_param *evt = (typeof(evt))param;

        wboard_logi("BK_BLE_GAP_CONNECT_COMPLETE_EVT %02x:%02x:%02x:%02x:%02x:%02x status 0x%x role %d hci_handle 0x%x",
                    evt->remote_bda[5],
                    evt->remote_bda[4],
                    evt->remote_bda[3],
                    evt->remote_bda[2],
                    evt->remote_bda[1],
                    evt->remote_bda[0],
                    evt->status,
                    evt->link_role,
                    evt->hci_handle
                   );
    }
    break;

    case BK_BLE_GAP_DISCONNECT_COMPLETE_EVT:
    {
        struct ble_disconnect_complete_param *evt = (typeof(evt))param;

        wboard_logi("BK_BLE_GAP_DISCONNECT_COMPLETE_EVT %02x:%02x:%02x:%02x:%02x:%02x %d status 0x%x reason 0x%x hci_handle 0x%x",
                    evt->remote_bda[5],
                    evt->remote_bda[4],
                    evt->remote_bda[3],
                    evt->remote_bda[2],
                    evt->remote_bda[1],
                    evt->remote_bda[0],
                    evt->remote_bda_type,
                    evt->status,
                    evt->reason,
                    evt->hci_handle
                   );

    }
    break;

    case BK_BLE_GAP_EXT_ADV_SET_RAND_ADDR_COMPLETE_EVT:
    {
        struct ble_adv_set_rand_addr_cmpl_evt_param *pm = (typeof(pm))param;

        if (pm->status)
        {
            wboard_loge("set adv rand addr err %d", pm->status);
        }

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore( &s_ble_sema );
        }
    }
    break;

    case BK_BLE_GAP_EXT_ADV_PARAMS_SET_COMPLETE_EVT:
    {
        struct ble_adv_params_set_cmpl_evt_param *pm = (typeof(pm))param;

        if (pm->status)
        {
            wboard_loge("set adv param err 0x%x", pm->status);
        }

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore(&s_ble_sema);
        }
    }
    break;

    case BK_BLE_GAP_EXT_ADV_DATA_SET_COMPLETE_EVT:
    {
        struct ble_adv_data_set_cmpl_evt_param *pm = (typeof(pm))param;

        if (pm->status)
        {
            wboard_loge("set adv data err %d", pm->status);
        }

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore(&s_ble_sema);
        }
    }
    break;

    case BK_BLE_GAP_EXT_ADV_DATA_RAW_SET_COMPLETE_EVT:
    {
        struct ble_adv_data_raw_set_cmpl_evt_param *pm = (typeof(pm))param;

        if (pm->status)
        {
            wboard_loge("set raw adv data err %d", pm->status);
        }

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore(&s_ble_sema);
        }
    }
    break;

    case BK_BLE_GAP_EXT_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
    {
        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore(&s_ble_sema);
        }
    }
    break;

    case BK_BLE_GAP_EXT_ADV_START_COMPLETE_EVT:
    {
        struct ble_adv_start_cmpl_evt_param *pm = (typeof(pm))param;

        if (pm->status)
        {
            wboard_loge("set adv enable err %d", pm->status);
        }

        wboard_logi("pls disable adv before remove pair !!!");

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore(&s_ble_sema);
        }
    }
    break;

    case BK_BLE_GAP_EXT_ADV_STOP_COMPLETE_EVT:
    {
        struct ble_adv_stop_cmpl_evt_param *pm = (typeof(pm))param;

        if (pm->status)
        {
            wboard_loge("set adv disable err %d", pm->status);
        }

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore(&s_ble_sema);
        }
    }
    break;

    case BK_BLE_GAP_EXT_ADV_SET_REMOVE_COMPLETE_EVT:
    {
        struct ble_adv_set_remove_cmpl_evt_param *pm = (typeof(pm))param;

        if (pm->status)
        {
            wboard_loge("remove adv set err %d", pm->status);
        }

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore(&s_ble_sema);
        }
    }
    break;

    default:
        break;
    }

}

int wifi_boarding_init(ble_boarding_info_t *info)
{
    bt_err_t ret = BK_FAIL;

    s_ble_boarding_info = info;
    enable_ble_split_pkt = false;
    if(!s_ble_sema)
    {
        ret = rtos_init_semaphore(&s_ble_sema, 1);

        if (ret != 0)
        {
            wboard_loge("rtos_init_semaphore err %d", ret);
            return -1;
        }
    }

    bk_ble_gap_register_callback(dm_ble_gap_common_cb);

    bk_ble_gatts_register_callback(wifi_boarding_gatts_cb);

    ret = bk_ble_gatts_app_register(0);

    if (ret)
    {
        wboard_loge("reg err %d", ret);
        return -1;
    }

    ret = rtos_get_semaphore(&s_ble_sema, SYNC_CMD_TIMEOUT_MS);

    if (ret != kNoErr)
    {
        wboard_loge("rtos_get_semaphore reg err %d", ret);
        return -1;
    }

    bk_ble_gatts_create_attr_tab(s_gatts_attr_db_service_boarding, s_gatts_if, sizeof(s_gatts_attr_db_service_boarding) / sizeof(s_gatts_attr_db_service_boarding[0]), 30);

    if (ret != 0)
    {
        wboard_loge("bk_ble_gatts_create_attr_tab err %d", ret);
        return -1;
    }

    ret = rtos_get_semaphore(&s_ble_sema, SYNC_CMD_TIMEOUT_MS);

    if (ret != kNoErr)
    {
        wboard_loge("rtos_get_semaphore err %d", ret);
        return -1;
    }

    bk_ble_gatts_start_service(s_service_attr_handle);

    ret = rtos_get_semaphore(&s_ble_sema, SYNC_CMD_TIMEOUT_MS);

    if (ret != kNoErr)
    {
        wboard_loge("rtos_get_semaphore err %d", ret);
        return -1;
    }

#if CONFIG_SENTINO_IOT
    sentino_ble_init(bsp_indicate, bsp_wifi_connect, bsp_wifi_scan);
#endif

    return BK_OK;
}

int wifi_boarding_deinit(void)
{
    int32_t ret = 0;

    wboard_logw("");

#if CONFIG_SENTINO_IOT
    sentino_ble_deinit();
#endif

    if (s_ble_boarding_info->ssid_value)
    {
        os_free(s_ble_boarding_info->ssid_value);
    }

    if (s_ble_boarding_info->password_value)
    {
        os_free(s_ble_boarding_info->password_value);
    }


    ret = bk_ble_gatts_app_unregister(s_gatts_if);

    if (ret)
    {
        wboard_loge("unreg err %d", ret);
    }

    ret = rtos_get_semaphore(&s_ble_sema, SYNC_CMD_TIMEOUT_MS);

    if (ret != kNoErr)
    {
        wboard_loge("rtos_get_semaphore unreg err %d", ret);
    }

    os_memset(s_ble_boarding_info, 0, sizeof(*s_ble_boarding_info));

    if (s_ble_sema)
    {
        ret = rtos_deinit_semaphore(&s_ble_sema);

        if (ret != 0)
        {
            wboard_loge("rtos_deinit_semaphore err %d", ret);
            return -1;
        }

        s_ble_sema = NULL;
    }

    bk_ble_gatts_register_callback(NULL);

    s_gatts_if = 0;
    s_prop_cli_config = 0;
    s_conn_ind = ~0;
    s_service_attr_handle = INVALID_ATTR_HANDLE;
    s_char_attr_handle = INVALID_ATTR_HANDLE;
    s_char_desc_attr_handle = INVALID_ATTR_HANDLE;
    s_char_write_char_handle = INVALID_ATTR_HANDLE;

    return BK_OK;
}

void dm_ble_gap_get_identity_addr(uint8_t *addr)
{
    uint8_t *identity_addr = addr;
    bk_get_mac((uint8_t *)identity_addr, MAC_TYPE_BLUETOOTH);

    for (int i = 0; i < BK_BD_ADDR_LEN / 2; i++)
    {
        uint8_t tmp = identity_addr[i];
        identity_addr[i] = identity_addr[BK_BD_ADDR_LEN - 1 - i];
        identity_addr[BK_BD_ADDR_LEN - 1 - i] = tmp;
    }
}

int wifi_boarding_adv_start(void)
{
    bt_err_t ret = BK_FAIL;

    bk_bd_addr_t current_addr = {0}, identity_addr = {0};
    char adv_name[64] = {0};

    dm_ble_gap_get_identity_addr(identity_addr);

    os_memcpy(current_addr, identity_addr, sizeof(identity_addr));

    current_addr[5] |= 0xc0;
    current_addr[0]++;

    snprintf((char *)(adv_name), sizeof(adv_name) - 1, "RY");

    wboard_logi("adv name %s", adv_name);

    ret = bk_ble_gap_set_device_name(adv_name);

    if (ret)
    {
        wboard_loge("bk_ble_gap_set_device_name err %d", ret);
        goto error;
    }

    bk_ble_gap_ext_adv_params_t adv_param =
    {
        .type = BK_BLE_GAP_SET_EXT_ADV_PROP_LEGACY_IND,
        .interval_min = 120 * 1,
        .interval_max = 160 * 1,
        .channel_map = BK_ADV_CHNL_ALL,
        .filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
        .primary_phy = BK_BLE_GAP_PRI_PHY_1M,
        .secondary_phy = BK_BLE_GAP_PHY_1M,
        .sid = 0,
        .scan_req_notif = 0,
        .own_addr_type = BLE_ADDR_TYPE_RANDOM,//BLE_ADDR_TYPE_PUBLIC,
    };

    ret =  bk_ble_gap_set_adv_params(ADV_HANDLE, &adv_param);

    if (ret != kNoErr)
    {
        wboard_loge("set adv param err %d", ret);
        goto error;
    }

    ret = rtos_get_semaphore(&s_ble_sema, SYNC_CMD_TIMEOUT_MS);

    if (ret != kNoErr)
    {
        wboard_loge("wait set adv param err %d", ret);
        goto error;
    }

    ret = bk_ble_gap_set_adv_rand_addr(ADV_HANDLE, current_addr);

    if (ret)
    {
        wboard_loge("bk_ble_gap_set_adv_rand_addr err %d", ret);
        goto error;
    }

    ret = rtos_get_semaphore(&s_ble_sema, SYNC_CMD_TIMEOUT_MS);

    if (ret != kNoErr)
    {
        wboard_loge("wait set adv rand addr err %d", ret);
        goto error;
    }

#define BEKEN_COMPANY_ID                    (0x05F0)
#define BOARDING_UUID                       (0xA101)

#if 0
    const uint8_t baording_service_uuid[16] =
    {
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
        0x00, 0x10, 0x00, 0x00,
        BOARDING_UUID & 0xff, (BOARDING_UUID >> 8) & 0xff, 0x00, 0x00
    };

    const uint8_t manuf_data[] = {BEKEN_COMPANY_ID & 0xFF, BEKEN_COMPANY_ID >> 8};

    bk_ble_adv_data_t adv_data =
    {
        .set_scan_rsp = 0,
        .include_name = 1,
        .min_interval = 0x0006,
        .max_interval = 0x0010,
        .appearance = 0,
        .manufacturer_len = sizeof(manuf_data),
        .p_manufacturer_data = (void *)manuf_data,
        .service_data_len = 0,
        .p_service_data = NULL,
        .service_uuid_len = sizeof(baording_service_uuid),
        .p_service_uuid = (void *)baording_service_uuid,
        .flag = 0x06,
    };

    ret = bk_ble_gap_set_adv_data((bk_ble_adv_data_t *)&adv_data);

    if (ret)
    {
        wboard_loge("bk_ble_gap_set_adv_data err %d", ret);
        goto error;
    }

#else
    uint8_t adv_data[251] = {0};
    uint32_t adv_index = 0, len_index = 0;

    /* ADV data layout (must fit 31 bytes):
     * Flags(3B) + Service UUIDs(4B) + Service Data(6B) + Manufacturer Data(16B) = 29B
     * Name is omitted here — already set via bk_ble_gap_set_device_name() */

    /* flags: 3B */
    len_index = adv_index;
    adv_data[adv_index++] = 0x00;
    adv_data[adv_index++] = BK_BLE_AD_TYPE_FLAG;
    adv_data[adv_index++] = 0x06;
    adv_data[len_index] = 2;

    /* Service UUIDs (AD type 0x03): 4B */
    len_index = adv_index;
    adv_data[adv_index++] = 0x00;
    adv_data[adv_index++] = 0x03; /* Complete List of 16-bit Service UUIDs */
    adv_data[adv_index++] = BOARDING_UUID & 0xFF;
    adv_data[adv_index++] = BOARDING_UUID >> 8;
    adv_data[len_index] = 3;

    /* Service Data (AD type 0x16): UUID(2B) + Flag(1B) + PID = variable */
    {
        const char *pid_str = sentino_provision_get_pid();
        uint8_t pid_len = strlen(pid_str);

        len_index = adv_index;
        adv_data[adv_index++] = 0x00;
        adv_data[adv_index++] = BK_BLE_AD_TYPE_SERVICE_DATA;
        adv_data[adv_index++] = BOARDING_UUID & 0xFF;
        adv_data[adv_index++] = BOARDING_UUID >> 8;
        adv_data[adv_index++] = 0x00; /* Flag */
        memcpy(&adv_data[adv_index], pid_str, pid_len);
        adv_index += pid_len;
        adv_data[len_index] = 1 + 2 + 1 + pid_len; /* type + UUID(2B) + Flag(1B) + PID */
    }

    /* Set ADV data (Flags + Service UUIDs + Service Data) */
    ret = bk_ble_gap_set_adv_data_raw(0, adv_index, (const uint8_t *)adv_data);

    if (ret)
    {
        wboard_loge("bk_ble_gap_set_adv_data_raw err %d", ret);
        goto error;
    }

#endif

    ret = rtos_get_semaphore(&s_ble_sema, SYNC_CMD_TIMEOUT_MS);

    if (ret != kNoErr)
    {
        wboard_loge("wait set adv data err %d", ret);
        goto error;
    }

    /* Scan Response: Name + Manufacturer Data (per ref-ble.md §2.2) */
    {
        uint8_t scan_rsp[251] = {0};
        uint32_t sr_index = 0, sr_len_index = 0;

        /* Complete Local Name (AD type 0x09) */
        sr_len_index = sr_index;
        scan_rsp[sr_index++] = 0x00;
        scan_rsp[sr_index++] = 0x09; /* Complete Local Name */
        uint8_t name_len = strlen((char *)adv_name);
        memcpy(&scan_rsp[sr_index], adv_name, name_len);
        sr_index += name_len;
        scan_rsp[sr_len_index] = name_len + 1;

        /* Manufacturer Data (ref-ble.md §2.2): Company ID(2B) + fields(6B) + MAC(6B) */
        {
            uint8_t ble_mac[6] = {0};
            bk_get_mac(ble_mac, MAC_TYPE_BLUETOOTH);

            const char *dev_uuid = "";
#if CONFIG_SENTINO_IOT
            /* Sentino UUID = device triple. Adapter lazy-loads on first call
             * (BLE adv starts before engine init); returns "" if UNAUTHORIZED. */
            dev_uuid = sentino_provision_get_uuid();
#endif
            uint8_t uuid_len = strlen(dev_uuid);

            sr_len_index = sr_index;
            scan_rsp[sr_index++] = 0x00;
            scan_rsp[sr_index++] = BK_BLE_AD_TYPE_MANU;
            scan_rsp[sr_index++] = 0x00; /* Company ID low (0x0000 per doc) */
            scan_rsp[sr_index++] = 0x00; /* Company ID high */
            scan_rsp[sr_index++] = 0x00; /* Config FLAG: provisioning, unbound, WiFi disconnected */
            scan_rsp[sr_index++] = 0x03; /* Proto Version: dual-mode (BLE + WiFi) */
            scan_rsp[sr_index++] = 0x02; /* Encrypt Method: plaintext */
            scan_rsp[sr_index++] = 0x00; /* Comm Ability high */
            scan_rsp[sr_index++] = 0x05; /* Comm Ability low: bit0=BLE bind, bit2=WiFi 2.4G */
            scan_rsp[sr_index++] = 0x00; /* ID Type: UUID (WiFi device default) */
            memcpy(&scan_rsp[sr_index], dev_uuid, uuid_len);
            sr_index += uuid_len;
            scan_rsp[sr_len_index] = sr_index - sr_len_index - 1;
        }

        ret = bk_ble_gap_set_scan_rsp_data_raw(0, sr_index, scan_rsp);
        if (ret) {
            wboard_loge("set scan rsp err %d", ret);
            goto error;
        }

        ret = rtos_get_semaphore(&s_ble_sema, SYNC_CMD_TIMEOUT_MS);
        if (ret != kNoErr) {
            wboard_loge("wait set scan rsp err %d", ret);
            goto error;
        }
    }

    const bk_ble_gap_ext_adv_t ext_adv =
    {
        .instance = 0,
        .duration = 0,
        .max_events = 0,
    };

    ret = bk_ble_gap_adv_start(1, &ext_adv);

    if (ret)
    {
        wboard_loge("bk_ble_gap_adv_start err %d", ret);
        goto error;
    }

    ret = rtos_get_semaphore(&s_ble_sema, SYNC_CMD_TIMEOUT_MS);

    if (ret != kNoErr)
    {
        wboard_loge("wait set adv enable err %d", ret);
        goto error;
    }

error:
    return 0;

}

int wifi_boarding_adv_stop(void)
{
    int32_t ret = 0;

    if(bk_bluetooth_get_status() != BK_BLUETOOTH_STATUS_ENABLED)
    {
        wboard_loge("bluetooth not init !!!");
        return BK_FAIL;
    }

    wboard_logi("");

    const uint8_t ext_adv_inst[] = {ADV_HANDLE};
    ret = bk_ble_gap_adv_stop(sizeof(ext_adv_inst) / sizeof(ext_adv_inst[0]), ext_adv_inst);

    if (ret)
    {
        wboard_loge("bk_ble_gap_adv_stop err %d", ret);
        return -1;
    }

    ret = rtos_get_semaphore(&s_ble_sema, SYNC_CMD_TIMEOUT_MS);

    if (ret != kNoErr)
    {
        wboard_loge("wait stop adv err %d", ret);
        return -1;
    }

    ret = bk_ble_gap_adv_set_remove(ADV_HANDLE);

    if (ret)
    {
        wboard_loge("bk_ble_gap_adv_set_remove err %d", ret);
        return -1;
    }

    ret = rtos_get_semaphore(&s_ble_sema, SYNC_CMD_TIMEOUT_MS);

    if (ret != kNoErr)
    {
        wboard_loge("wait remove adv err %d", ret);
        return -1;
    }

    return BK_OK;
}

int wifi_boarding_notify(uint8_t *data, uint16_t length)
{
    if (s_conn_ind == (uint16_t)~0)
    {
        wboard_loge("BLE is disconnected, can not send data !!!");
        return BK_FAIL;
    }
    else
    {
        wboard_logi("len %d", length);
        bk_ble_gatts_send_indicate(s_gatts_if, s_conn_ind, s_char_attr_handle, length, data, 0);
        return BK_OK;
    }
}

