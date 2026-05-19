# sentino_iot_sdk — Sentino IoT control plane SDK.
#
# Layer 3 of the firmware stack (see plans/drifting-wishing-storm.md):
#   - sentino_app/    boot dispatcher, device info (triple), provisioning
#   - sentino_mqtt/   MQTT signalling app + broker CA
#   - sentino_ble/    BLE V1 provisioning protocol codec
#
# The SDK does NOT depend on any RTC backend or BLE GATT vendor — those
# are wired in by sentino_interface/ via callback registration. The SDK
# only knows about generic transports (mqtts) and platform primitives.
#
# Public include strategy: all subdirs are exposed flat. Callers write
#   #include "sentino_mqtt.h"
#   #include "sentino_dev_info.h"
#   #include "sentino_iot_engine.h"
#   #include "sentino_ble_v1.h"
# regardless of which subdir the header lives in.

set(srcs)
set(incs sentino_app sentino_mqtt sentino_ble sentino_http_ota sentino_factory sentino_command)

if (CONFIG_SENTINO_IOT)
list(APPEND srcs
    sentino_app/sentino_dev_info.c
    sentino_app/sentino_iot_engine.c
    sentino_mqtt/sentino_mqtt.c
    sentino_mqtt/sentino_mqtt_dp.c
    sentino_mqtt/sentino_broker_ca.c
    sentino_ble/sentino_ble_v1.c
    sentino_ble/sentino_ble_proto.c
    sentino_http_ota/sentino_ota.c
    sentino_factory/sentino_dynamic_register.c
    sentino_command/sentino_command_bus.c
    sentino_command/sentino_command_router.c
)
endif()

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES bk_common bk_app_event lwip_intf_v2_1 mbedtls psa_mbedtls
                  json bk_bt network_transfer ali_mqtt ota
)
