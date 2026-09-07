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

void BSP_BLE_Init(void);     /* GAP Peripheral init + custom service */
void BSP_BLE_TaskProcess(void);

#endif /* __BSP_BLE_H__ */
