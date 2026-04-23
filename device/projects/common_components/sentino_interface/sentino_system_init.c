#include "sentino_system_init.h"
#include "sentino_rtc_export.h"

void sentino_interface_init(void)
{
    sentino_rtc_export_init();
}
