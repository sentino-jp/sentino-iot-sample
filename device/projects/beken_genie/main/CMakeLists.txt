set(incs . include)
set(srcs)

list(APPEND srcs
    app_main.c
)

if (CONFIG_SYS_CPU0)
list(APPEND srcs
    audio_para.c
)
if (CONFIG_SENTINO_IOT)
list(APPEND srcs
    app_dp_handler.c
)
if (CONFIG_BAT_MONITOR)
list(APPEND srcs
    app_battery_dp.c
)
endif()
if (CONFIG_ENABLE_AGORA_DATASTREAM)
list(APPEND srcs
    genie_action_display_emotion.c
)
endif()
endif()
endif()

if (CONFIG_OVERRIDE_FLASH_PARTITION)
    list(APPEND srcs
        vendor_flash.c
    )
endif()

armino_component_register(SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES bk_init lwip_intf_v2_1 agora-iot-sdk media_service multimedia avdk_utils mbedtls json wanson
    bk_wanson_asr bk_factory_config bk_nfc bk_app_event bk_countdown bk_led_blink bk_motor bk_boarding_service
    bk_smart_config bk_bt bk_key_app asr audio_engine video_engine network_transfer sentino_interface
)
