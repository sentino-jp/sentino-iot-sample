# platform_adapter — BSP layer

Layer 4 of the firmware stack (see `plans/drifting-wishing-storm.md`).
Wraps the BK7258 / armino / FreeRTOS HAL behind a small `bsp_*` surface
so the higher layers (`sentino_iot_sdk/`, `sentino_interface/`,
`beken_genie/main/`) eventually depend on this single component instead
of `<components/...>`, `<os/...>`, `bk_ef.h`, `modules/wifi.h`, etc.

## Status — scaffold only

Today this component exists so the dependency direction is established.
SDK code still calls armino headers directly. New code should prefer
`bsp_*`; old code is migrated opportunistically.

| Module | What it wraps | Migration status |
|---|---|---|
| `bsp_log.h` | `<components/log.h>` BK_LOG* macros | header-only alias, no callers yet |

`bsp_flash` was prototyped and pulled — wrapping `bk_ef` is real work that
needs to coordinate with `easy_flash`'s component-include topology, and
nothing in the SDK uses the wrapper yet. Build it when the first SDK
caller is ready to switch.

## Why this scaffold and not full migration?

A full BSP cut means rewriting every `BK_LOGI`, `bk_ef_*`, `wifi_*`,
`bk_ble_*`, `rtos_*` callsite in the SDK. That's a long-tail effort and
not what unblocks the next deliverable (real OTA / dynamic register).
The scaffold lets us:

1. Mark the layer as a real component (visible in EXTRA_COMPONENTS_DIRS).
2. Set the API surface (one `bsp_log.h`, one `bsp_flash.h`) so when we
   migrate, we know what we're targeting.
3. Avoid pretending Phase 4 changed nothing — we have something real to
   point at when adding the next platform target.

## Adding to a project

```cmake
# beken_genie/CMakeLists.txt
$ENV{ARMINO_PATH}/../../projects/common_components/platform_adapter

# any consumer's CMakeLists.txt
PRIV_REQUIRES platform_adapter
```
