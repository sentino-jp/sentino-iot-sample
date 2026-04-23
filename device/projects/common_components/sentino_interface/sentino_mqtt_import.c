#include "sentino_mqtt_import.h"
#include "sentino_mqtt_dp.h"

void Register_Sentino_Dp_Set_Cb(dp_set_cb_t cb)
{
    sentino_register_dp_set_cb(cb);
}

int Sentino_Dp_Report_Export(const dp_obj_t *dp)
{
    return Sentino_Dp_Report(dp);
}
