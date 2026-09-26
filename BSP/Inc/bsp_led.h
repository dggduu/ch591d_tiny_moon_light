#ifndef __BSP_LED_H__
#define __BSP_LED_H__

#include <stdint.h>
#include <stdbool.h>
#include "CH59x_common.h"

/* Existing PWM/GPIO pin mapping from the schematic */
#define PWM_BLUE    CH_PWM7
#define PWM_RED     CH_PWM4
#define PWM_GREEN   CH_PWM5

#define IO_BLUE     GPIO_Pin_4
#define IO_RED      GPIO_Pin_12
#define IO_GREEN    GPIO_Pin_13

/* Light mode, stored in the characteristic / flash */
#define BSP_LIGHT_MODE_FIXED       0u
#define BSP_LIGHT_MODE_GRADIENT    1u
#define BSP_LIGHT_MODE_BREATHING   2u
#define BSP_LIGHT_MODE_COLORFUL    3u

#define BSP_LIGHT_COLOR_MAX        255u
#define BSP_LIGHT_BRIGHTNESS_MAX   100u
#define BSP_LIGHT_BRIGHTNESS_LIMIT 100u   /* 实际生效上限，硬件足够亮 */

#define BSP_LIGHT_PWM_CYCLE        256u

typedef struct __attribute__((packed))
{
    uint8_t  power;       /* 0=OFF, 1=ON */
    uint8_t  brightness;  /* 0..BSP_LIGHT_BRIGHTNESS_LIMIT */
    uint8_t  mode;        /* fixed/gradient/breathing/colorful */
    uint8_t  rgb[3];      /* R,G,B 0..255 */
    uint16_t colorTemp;   /* 2700..6500, for future RGBW/dual-color */
    uint8_t  effectSpeed; /* 0..100, gradient/breathing speed */
} BSP_LightState_t;

void    BSP_LED_Init(void);
void    BSP_LED_SetState(const BSP_LightState_t *pState);
void    BSP_LED_GetState(BSP_LightState_t *pOut);
bool    BSP_LED_IsPowerOn(void);
void    BSP_LED_SetPower(uint8_t on);
void    BSP_LED_UpdateOutput(void);
void    BSP_LED_TaskTick(void);

#endif /* __BSP_LED_H__ */