#include "sentino_mqtt_import.h"
#include "sentino_mqtt_dp.h"
#include "sentino_mqtt.h"          /* sentino_mqtt_publish_nfc_report */
#include "sentino_iot_engine.h"    /* sentino_engine_register_cloud_ready_cb */

void Register_Sentino_Dp_Set_Cb(dp_set_cb_t cb)
{
    sentino_register_dp_set_cb(cb);
}

int Sentino_Dp_Report_Export(const dp_obj_t *dp)
{
    return Sentino_Dp_Report(dp);
}

int Sentino_Dp_Report_Many_Export(const dp_obj_t *dps, size_t count)
{
    return Sentino_Dp_Report_Many(dps, count);
}

int Sentino_Nfc_Report_Export(const unsigned char *nfc_id, int len, int only_report)
{
    if (!nfc_id || len <= 0) return -1;
    return sentino_mqtt_publish_nfc_report(nfc_id, len, only_report);
}

void Register_Sentino_Cloud_Ready_Cb(void (*cb)(void))
{
    sentino_engine_register_cloud_ready_cb(cb);
}

void Register_Sentino_Bind_Ack_Cb(void (*cb)(int res))
{
    sentino_mqtt_register_bind_ack_cb(cb);
}
