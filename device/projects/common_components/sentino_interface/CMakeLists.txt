set(srcs)
set(incs .)

if (CONFIG_SENTINO_IOT)
list(APPEND srcs
    sentino_provision_import.c
    sentino_mqtt_import.c
    sentino_rtc_export.c
    sentino_system_init.c
)
if (CONFIG_ENABLE_AGORA_DATASTREAM)
list(APPEND srcs
    sentino_conv_ai_command.c
)
endif()
endif()

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    # sentino_iot_sdk must be public REQUIRES (not PRIV) because
    # sentino_mqtt_import.h re-exports dp_obj_t/dp_type_e via
    # #include "sentino_mqtt_dp.h" — consumers (main, etc.) need that
    # include path too.
    REQUIRES sentino_iot_sdk
    PRIV_REQUIRES bk_common network_transfer agora-iot-sdk audio_engine main json psa_mbedtls
)
