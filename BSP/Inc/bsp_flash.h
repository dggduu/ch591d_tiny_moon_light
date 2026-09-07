#ifndef __BSP_FLASH_H__
#define __BSP_FLASH_H__

#include <stdint.h>
#include "bsp_led.h"
#include "bsp_rtc.h"

/*
 * Data-Flash offset used by this application.
 * The BLE SNV area is near 0x7000, so use the 0x6000 4K sector.
 */
#define BSP_NVM_ADDR      0x6000u
#define BSP_NVM_MAGIC     0x4E4C4544u   /* "DELN" little-endian-ish magic */

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t len;
    uint16_t crc;
    BSP_LightState_t light;
    BSP_RtcTimer_t   timer;
} BSP_Nvm_t;

void    BSP_NVM_LoadDefault(BSP_Nvm_t *pNvm);
uint8_t BSP_NVM_Load(BSP_Nvm_t *pNvm);       /* 0=ok, non-zero=fail/empty */
uint8_t BSP_NVM_Save(const BSP_Nvm_t *pNvm); /* 0=ok, non-zero=fail */

#endif /* __BSP_FLASH_H__ */
