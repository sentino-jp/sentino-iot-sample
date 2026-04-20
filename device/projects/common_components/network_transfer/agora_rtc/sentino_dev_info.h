#ifndef __SENTINO_DEV_INFO_H__
#define __SENTINO_DEV_INFO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "sentino_mqtt.h"  /* for SENTINO_UUID_SIZE / SENTINO_KEY_SIZE / SENTINO_PID_SIZE */

/* ────────────────────────────────────────────────────────────────────
 *  Compile-time configuration
 * ──────────────────────────────────────────────────────────────────── */

/* Comment out for production builds. When defined, an empty NVS triple
 * is auto-populated on boot from the SENTINO_TEST_* values below — so the
 * device behaves like the legacy SENTINO_MOCK_* path. When undefined,
 * an empty triple leaves the device in SENTINO_DEV_UNAUTHORIZED. */
#define SENTINO_TRIPLE_TEST

/* Per-build product key. Reference firmware bakes PID into firmware
 * (rino_iot_process.c PRODUCT_KEY); Sentino does the same. */
#define SENTINO_DEFAULT_PID         "vqB8C7fniWRLWL"

#ifdef SENTINO_TRIPLE_TEST
#define SENTINO_TEST_PID            SENTINO_DEFAULT_PID
#define SENTINO_TEST_UUID           "ct01kQBXBK7h63H8"
#define SENTINO_TEST_SECRET         "0c6ceda19a574413b92f0e1185f73c80"
#define SENTINO_TEST_MAC            "444AD60C228F"
#endif

/* ────────────────────────────────────────────────────────────────────
 *  Types
 * ──────────────────────────────────────────────────────────────────── */

#define SENTINO_TRIPLE_MAGIC        0x534E544FU  /* "SNTO" */
#define SENTINO_TRIPLE_FLAG_VALID   0xA5A5A5A5U
#define SENTINO_MAC_STR_SIZE        18           /* "AA:BB:CC:DD:EE:FF\0" */
#define SENTINO_REGISTER_CODE_SIZE  64

typedef struct {
    char Uuid[SENTINO_UUID_SIZE];
    char Secret[SENTINO_KEY_SIZE];
    char Mac[SENTINO_MAC_STR_SIZE];
    char Pid[SENTINO_PID_SIZE];
    char Register_Code[SENTINO_REGISTER_CODE_SIZE];
} sentino_triple_t;

/* On-flash record. Magic + flag let us detect blank / corrupt NVS. */
typedef struct {
    uint32_t magic;
    uint32_t flag_valid;
    sentino_triple_t triple;
    uint8_t  reserve[64];
} sentino_dev_triple_record_t;

typedef enum {
    SENTINO_DEV_UNAUTHORIZED = 0,  /* no triple in flash, no test triple supplied */
    SENTINO_DEV_AUTHORIZED   = 1,  /* triple available */
} sentino_dev_state_t;

/* ────────────────────────────────────────────────────────────────────
 *  API — load semantics mirror Rino_Load_Dev_Info in the reference.
 * ──────────────────────────────────────────────────────────────────── */

/**
 * Load triple from NVS, optionally seeding/overwriting it with test_triple.
 *
 * Order of operations:
 *   1. Read NVS key d_stn_triple. If magic+flag valid, take that triple.
 *   2. If test_triple != NULL and differs from what's in flash,
 *      overwrite flash + cache with test_triple.
 *   3. If neither flash nor test_triple yielded a triple → state becomes
 *      SENTINO_DEV_UNAUTHORIZED. Caller (engine init) should refuse to
 *      start MQTT/RTC and either trigger dynamic register (TODO) or wait
 *      for factory burn-in.
 *
 * @param pid_default  Fallback PID if neither flash nor test_triple has one
 *                     (for forward-compat with older flash records).
 * @param test_triple  Optional test triple. Pass NULL in production.
 * @return 0 on AUTHORIZED, -1 on UNAUTHORIZED.
 */
int sentino_dev_info_load(const char *pid_default,
                          const sentino_triple_t *test_triple);

/** Get the loaded triple. Returns NULL if not loaded or UNAUTHORIZED. */
const sentino_triple_t *sentino_dev_info_get_triple(void);

/** Current authorization state. */
sentino_dev_state_t sentino_dev_info_get_state(void);

/** Persist the in-RAM triple to NVS. Returns 0 on success. */
int sentino_dev_info_save(void);

/** Wipe the triple from both RAM and NVS. State becomes UNAUTHORIZED. */
int sentino_dev_info_reset(void);

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_DEV_INFO_H__ */
