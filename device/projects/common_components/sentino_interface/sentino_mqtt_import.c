#include "sentino_mqtt_import.h"
#include "sentino_mqtt_dp.h"
#include "sentino_mqtt.h"   /* sentino_mqtt_publish_nfc_report */

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
