#ifndef __BSP_BLE_H__
#define __BSP_BLE_H__

#include <stdint.h>

/* Custom light-control service UUID */
#define BSP_BLE_SERVICE_UUID        0xFFF0u
#define BSP_BLE_POWER_UUID          0xFFF1u
#define BSP_BLE_BRIGHTNESS_UUID     0xFFF2u
#define BSP_BLE_MODE_UUID           0xFFF3u
#define BSP_BLE_COLOR_UUID          0xFFF4u
#define BSP_BLE_COLOR_TEMP_UUID     0xFFF5u
#define BSP_BLE_EFFECT_SPEED_UUID   0xFFF6u
#define BSP_BLE_TIMER_UUID          0xFFF7u
#define BSP_BLE_BATTERY_UUID        0xFFF8u
#define BSP_BLE_RTC_TIME_UUID       0xFFF9u
/*
 * 0xFFFA: 倒计时自动关灯（2 字节小端秒数）。
 *   0            = 取消
 *   30..900      = 从现在起 N 秒后关灯（由内部 RTC / 1s tick 计时）
 * 供 DSH 蓝牙灯插件做「0.5–15 分钟空闲自动关灯」——比 0xFFF7 的墙上时钟
 * 定时器粒度更细，手机端也不需要先校准时间。
 */
#define BSP_BLE_COUNTDOWN_UUID      0xFFFAu

/** 倒计时可取的最短/最长秒数。 */
#define BSP_BLE_COUNTDOWN_MIN_SEC   30u
#define BSP_BLE_COUNTDOWN_MAX_SEC   900u

void BSP_BLE_Init(void);     /* GAP Peripheral init + custom service */
void BSP_BLE_TaskProcess(void);

#endif /* __BSP_BLE_H__ */
