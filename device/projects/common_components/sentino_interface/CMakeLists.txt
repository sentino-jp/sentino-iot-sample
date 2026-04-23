set(srcs)
set(incs .)

if (CONFIG_SENTINO_IOT)
list(APPEND srcs
    sentino_provision_import.c
    sentino_rtc_export.c
    sentino_system_init.c
)
endif()

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES bk_common network_transfer agora-iot-sdk audio_engine main
)
