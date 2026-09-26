#ifndef __BSP_BATTARY_H__
#define __BSP_BATTARY_H__

#include <stdint.h>
#include "CH59x_common.h"

/* GPIOB */
#define IO_BAT_EN   GPIO_Pin_11
/* GPIOA */
#define IO_BAT      GPIO_Pin_14
#define CH_BAT      CH_EXTIN_4

void    BSP_BAT_Init(void);
uint16_t BSP_BAT_GetMillivolt(void);   /* battery voltage * 1000 */
uint8_t BSP_BAT_GetPercent(void);      /* simple 3.3V-4.2V curve */
uint8_t BSP_BAT_GetLevel(void);        /* same as percent, for BLE report */

#endif /* __BSP_BATTARY_H__ */
