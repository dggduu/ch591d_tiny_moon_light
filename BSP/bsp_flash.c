#include <string.h>
#include "CH59x_common.h"
#include "bsp_flash.h"

static uint16_t calc_crc(const uint8_t *p, uint16_t len)
{
    uint16_t sum = 0;
    uint16_t i;
    for(i = 0; i < len; i++)
        sum = (uint16_t)(sum + p[i]);
    return sum;
}

void BSP_NVM_LoadDefault(BSP_Nvm_t *pNvm)
{
    if(!pNvm)
        return;
    memset(pNvm, 0, sizeof(*pNvm));
    pNvm->magic = BSP_NVM_MAGIC;
    pNvm->len = sizeof(*pNvm);
    pNvm->light.power = 0;
    pNvm->light.brightness = 80;
    pNvm->light.mode = BSP_LIGHT_MODE_FIXED;
    pNvm->light.rgb[0] = 255;
    pNvm->light.rgb[1] = 255;
    pNvm->light.rgb[2] = 255;
    pNvm->light.colorTemp = 4000;
    pNvm->light.effectSpeed = 20;

    pNvm->timer.enable = 0;
    pNvm->timer.hour = 22;
    pNvm->timer.minute = 0;
    pNvm->timer.repeat = BSP_RTC_REPEAT_MON | BSP_RTC_REPEAT_TUE |
                         BSP_RTC_REPEAT_WED | BSP_RTC_REPEAT_THU |
                         BSP_RTC_REPEAT_FRI;
    pNvm->timer.action = BSP_RTC_ACTION_OFF;
    pNvm->timer.brightness = 80;
    pNvm->timer.rgb[0] = 255;
    pNvm->timer.rgb[1] = 200;
    pNvm->timer.rgb[2] = 150;
    pNvm->timer.durationMin = 0;
    pNvm->crc = calc_crc((const uint8_t *)pNvm + 6, (uint16_t)sizeof(*pNvm) - 6);
}

uint8_t BSP_NVM_Load(BSP_Nvm_t *pNvm)
{
    __attribute__((aligned(4))) uint8_t buf[sizeof(BSP_Nvm_t)];
    BSP_Nvm_t *p = (BSP_Nvm_t *)(void *)buf;
    uint16_t crc;

    if(!pNvm)
        return 1;

    memset(buf, 0, sizeof(buf));
    EEPROM_READ(BSP_NVM_ADDR, buf, sizeof(buf));

    if(p->magic != BSP_NVM_MAGIC)
        return 2;
    if(p->len != sizeof(BSP_Nvm_t))
        return 3;

    crc = calc_crc((const uint8_t *)p + 6, (uint16_t)sizeof(*p) - 6);
    if(crc != p->crc)
        return 4;

    memcpy(pNvm, buf, sizeof(buf));
    return 0;
}

uint8_t BSP_NVM_Save(const BSP_Nvm_t *pNvm)
{
    __attribute__((aligned(4))) uint8_t buf[sizeof(BSP_Nvm_t)];
    BSP_Nvm_t *p = (BSP_Nvm_t *)(void *)buf;
    uint32_t s;

    if(!pNvm)
        return 1;

    memset(buf, 0, sizeof(buf));
    memcpy(buf, pNvm, sizeof(*pNvm));
    p->magic = BSP_NVM_MAGIC;
    p->len = sizeof(*pNvm);
    p->crc = 0;
    p->crc = calc_crc((const uint8_t *)p + 6, (uint16_t)sizeof(*p) - 6);

    s = EEPROM_ERASE(BSP_NVM_ADDR, EEPROM_BLOCK_SIZE);
    if(s)
        return 2;
    s = EEPROM_WRITE(BSP_NVM_ADDR, buf, sizeof(buf));
    if(s)
        return 3;
    return 0;
}
