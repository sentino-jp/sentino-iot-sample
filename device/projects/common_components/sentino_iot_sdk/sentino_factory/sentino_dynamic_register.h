#ifndef __SENTINO_DYNAMIC_REGISTER_H__
#define __SENTINO_DYNAMIC_REGISTER_H__

/* Sentino factory dynamic register — fetch the device triple from a
 * Sentino-hosted bootstrap endpoint at first boot, instead of relying
 * on the SENTINO_TRIPLE_TEST seed or factory burn-in.
 *
 * Today the device falls back to one of:
 *   - SENTINO_TRIPLE_TEST seed (dev/test builds)
 *   - persisted NVS triple (production builds, written at factory)
 *   - SENTINO_DEV_UNAUTHORIZED (production unprovisioned device)
 *
 * The eventual flow once Sentino exposes a register endpoint:
 *   1. Engine boots, sentino_dev_info_load() reports UNAUTHORIZED.
 *   2. Engine calls sentino_dynamic_register(&triple).
 *   3. This function POSTs registration code (typically printed on the
 *      box, or from a per-device QR) to a hardcoded bootstrap URL,
 *      receives {uuid, secret, mac, pid}, returns the triple.
 *   4. Engine persists via sentino_dev_info_save() and continues.
 *
 * SKELETON: returns -1 (not implemented). Sentino's exact register
 * protocol is not yet documented (ref-mqtt.md / ref-ble.md cover the
 * runtime surface only). Fill in once the spec lands. */

#include "sentino_dev_info.h"  /* sentino_triple_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 0 on success and fills *out, -1 on failure. */
int sentino_dynamic_register(sentino_triple_t *out);

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_DYNAMIC_REGISTER_H__ */
