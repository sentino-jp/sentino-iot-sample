#include "sentino_system_init.h"
#include "sentino_rtc_export.h"
#include "sentino_iot_engine.h"   /* sentino_engine_register_wifi_signal_query */
#include "sentino_wifi_import.h"  /* sentino_bsp_query_wifi_signal_quality */

void sentino_interface_init(void)
{
    sentino_rtc_export_init();

    /* Wire BK WiFi signal query into the SDK so the cloud-issued `ping`
     * handler can fill {signal, signalValue}. Ref: ref-mqtt §5.3. */
    sentino_engine_register_wifi_signal_query(sentino_bsp_query_wifi_signal_quality);
}
