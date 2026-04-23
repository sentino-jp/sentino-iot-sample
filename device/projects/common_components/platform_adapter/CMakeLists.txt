# platform_adapter — Layer 4 BSP scaffold (see README.md).
#
# Today: header-only. bsp_log.h aliases the BK_LOG* macros so SDK code
# can be ported off armino's <components/log.h> incrementally.
#
# Future: bsp_wifi/bsp_ble/bsp_flash/bsp_mutex/bsp_timer as armino
# calls get refactored out of the SDK.
#
# No SRCS yet — register an empty component so it shows up in the
# component graph and consumers can declare PRIV_REQUIRES platform_adapter.

armino_component_register(
    INCLUDE_DIRS .
    PRIV_REQUIRES bk_common
)
