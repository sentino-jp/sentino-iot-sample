#ifndef __BSP_LOG_H__
#define __BSP_LOG_H__

/* Thin alias over armino log macros so SDK code can `#include "bsp_log.h"`
 * and use BSP_LOG* without naming the underlying HAL. When porting to a
 * non-armino MCU, redirect these to that platform's log facility. */

#include <components/log.h>

#define BSP_LOGI(tag, fmt, ...)  BK_LOGI(tag, fmt, ##__VA_ARGS__)
#define BSP_LOGW(tag, fmt, ...)  BK_LOGW(tag, fmt, ##__VA_ARGS__)
#define BSP_LOGE(tag, fmt, ...)  BK_LOGE(tag, fmt, ##__VA_ARGS__)
#define BSP_LOGD(tag, fmt, ...)  BK_LOGD(tag, fmt, ##__VA_ARGS__)

#endif /* __BSP_LOG_H__ */
