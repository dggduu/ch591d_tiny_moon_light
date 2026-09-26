#ifndef __BSP_FLASH_H__
#define __BSP_FLASH_H__

#include <stdint.h>
#include "bsp_led.h"
#include "bsp_rtc.h"

#define BSP_NVM_SLOT_A_ADDR   0x2000u   /* relative to the DataFlash base */
#define BSP_NVM_SLOT_B_ADDR   0x3000u   /* relative to the DataFlash base */

#define BSP_NVM_MAGIC         0x4E32u   /* "N2" */
#define BSP_NVM_VERSION       0x0002u   /* bumped when the layout changes */

/*
 * Persistent light parameters. The on/off state is deliberately *not* stored:
 * the lamp always boots up switched off (see BSP_NVM_LoadDefault()).
 */
typedef struct __attribute__((packed))
{
    uint8_t  brightness;   /* 0..100 */
    uint8_t  mode;         /* fixed/gradient/breathing/colorful */
    uint8_t  rgb[3];       /* R,G,B */
    uint16_t colorTemp;    /* 2700..6500 */
    uint8_t  effectSpeed;  /* 0..100 */
} BSP_NvmLight_t;

/* Everything that survives a power cycle. */
typedef struct __attribute__((packed))
{
    BSP_NvmLight_t light;
    BSP_RtcTimer_t timer;
} BSP_Settings_t;

/* Current settings, kept in RAM and mirrored into the DataFlash on save. */
extern BSP_Settings_t g_settings;

void    BSP_NVM_LoadDefault(void);
uint8_t BSP_NVM_Load(void);    /* 0 = a valid record was restored */
uint8_t BSP_NVM_Save(void);    /* 0 = stored and read back successfully */

#endif /* __BSP_FLASH_H__ */
