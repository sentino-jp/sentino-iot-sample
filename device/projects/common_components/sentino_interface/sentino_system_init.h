#ifndef __SENTINO_SYSTEM_INIT_H__
#define __SENTINO_SYSTEM_INIT_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Wire all SDK ↔ business adapter callbacks (RTC handoff today; DP / OTA
 * import callbacks in future phases). Idempotent. Call once at boot before
 * the Sentino engine starts. */
void sentino_interface_init(void);

#ifdef __cplusplus
}
#endif
#endif /* __SENTINO_SYSTEM_INIT_H__ */
