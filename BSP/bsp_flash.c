#include <string.h>
#include "CH59x_common.h"
#include "bsp_flash.h"

#define BSP_NVM_SLOT_SIZE     ((uint16_t)sizeof(BSP_NvmRecord_t))
#define BSP_NVM_CRC_OFFSET    8u    /* start of the checksummed region (after len) */

typedef struct __attribute__((packed))
{
    uint16_t magic;
    uint16_t version;
    uint32_t serial;      /* newer serial wins */
    uint16_t len;         /* == sizeof(BSP_NvmRecord_t) */
    BSP_Settings_t settings;
    uint8_t  reserved[3];
    uint16_t crc;         /* checksum of the bytes before this field */
} BSP_NvmRecord_t;

#define BSP_NVM_CRC_LEN \
    ((uint16_t)(sizeof(BSP_NvmRecord_t) - BSP_NVM_CRC_OFFSET - sizeof(uint16_t)))

BSP_Settings_t g_settings;

/* ------------------------------------------------------------------ */

static uint16_t calc_crc(const uint8_t *p, uint16_t len)
{
    uint16_t sum = 0;
    uint16_t i;

    for (i = 0; i < len; i++)
        sum = (uint16_t)(sum + p[i]);

    return sum;
}

static uint8_t record_valid(const BSP_NvmRecord_t *pRec)
{
    if (pRec->magic != BSP_NVM_MAGIC)
        return 0;
    if (pRec->version != BSP_NVM_VERSION)
        return 0;
    if (pRec->len != BSP_NVM_SLOT_SIZE)
        return 0;

    return (uint8_t)(calc_crc((const uint8_t *)pRec + BSP_NVM_CRC_OFFSET,
                              BSP_NVM_CRC_LEN) == pRec->crc);
}

/* a > b, wrap safe */
static uint8_t serial_newer(uint32_t a, uint32_t b)
{
    return (uint8_t)((int32_t)(a - b) > 0);
}

static void record_read(uint32_t addr, BSP_NvmRecord_t *pRec)
{
    memset(pRec, 0, sizeof(*pRec));
    EEPROM_READ(addr, pRec, BSP_NVM_SLOT_SIZE);
}

static uint8_t record_write(uint32_t addr, const BSP_NvmRecord_t *pRec)
{
    __attribute__((aligned(4))) BSP_NvmRecord_t verify;
    uint32_t s;

    /* Erase granularity on CH59x is 256 bytes; erase one block covering the record. */
    s = EEPROM_ERASE(addr, EEPROM_BLOCK_SIZE);
    if (s)
        return 2;

    s = EEPROM_WRITE(addr, (void *)(uintptr_t)pRec, BSP_NVM_SLOT_SIZE);
    if (s)
        return 3;

    record_read(addr, &verify);
    if (!record_valid(&verify))
        return 4;

    return 0;
}

/* ------------------------------------------------------------------ */

void BSP_NVM_LoadDefault(void)
{
    memset(&g_settings, 0, sizeof(g_settings));

    g_settings.light.brightness = BSP_LIGHT_BRIGHTNESS_LIMIT;
    g_settings.light.mode = BSP_LIGHT_MODE_FIXED;
    g_settings.light.rgb[0] = 255;
    g_settings.light.rgb[1] = 255;
    g_settings.light.rgb[2] = 255;
    g_settings.light.colorTemp = 4000;
    g_settings.light.effectSpeed = 20;

    g_settings.timer.enable = 0;
    g_settings.timer.hour = 22;
    g_settings.timer.minute = 0;
    g_settings.timer.repeat = BSP_RTC_REPEAT_MON | BSP_RTC_REPEAT_TUE |
                              BSP_RTC_REPEAT_WED | BSP_RTC_REPEAT_THU |
                              BSP_RTC_REPEAT_FRI;
    g_settings.timer.action = BSP_RTC_ACTION_OFF;
    g_settings.timer.brightness = BSP_LIGHT_BRIGHTNESS_LIMIT;
    g_settings.timer.rgb[0] = 255;
    g_settings.timer.rgb[1] = 200;
    g_settings.timer.rgb[2] = 150;
    g_settings.timer.durationMin = 0;
}

uint8_t BSP_NVM_Load(void)
{
    BSP_NvmRecord_t recA;
    BSP_NvmRecord_t recB;
    uint8_t okA;
    uint8_t okB;

    record_read(BSP_NVM_SLOT_A_ADDR, &recA);
    record_read(BSP_NVM_SLOT_B_ADDR, &recB);

    okA = record_valid(&recA);
    okB = record_valid(&recB);

    if (okA && okB)
    {
        if (serial_newer(recA.serial, recB.serial))
            g_settings = recA.settings;
        else
            g_settings = recB.settings;
    }
    else if (okA)
    {
        g_settings = recA.settings;
    }
    else if (okB)
    {
        g_settings = recB.settings;
    }
    else
    {
        return 1;   /* nothing usable, the caller loads the defaults */
    }

    return 0;
}

uint8_t BSP_NVM_Save(void)
{
    BSP_NvmRecord_t recA;
    BSP_NvmRecord_t recB;
    BSP_NvmRecord_t rec;
    uint32_t target;
    uint8_t okA;
    uint8_t okB;
    uint8_t s;

    record_read(BSP_NVM_SLOT_A_ADDR, &recA);
    record_read(BSP_NVM_SLOT_B_ADDR, &recB);

    okA = record_valid(&recA);
    okB = record_valid(&recB);

    memset(&rec, 0, sizeof(rec));
    rec.magic = BSP_NVM_MAGIC;
    rec.version = BSP_NVM_VERSION;
    rec.len = BSP_NVM_SLOT_SIZE;
    rec.settings = g_settings;
    /* rec.crc is still 0 here, so it does not contribute to its own sum. */
    rec.crc = calc_crc((const uint8_t *)&rec + BSP_NVM_CRC_OFFSET, BSP_NVM_CRC_LEN);

    if (okA && okB)
    {
        if (serial_newer(recA.serial, recB.serial))
        {
            rec.serial = recA.serial + 1u;
            target = BSP_NVM_SLOT_B_ADDR;   /* overwrite the older copy */
        }
        else
        {
            rec.serial = recB.serial + 1u;
            target = BSP_NVM_SLOT_A_ADDR;
        }
    }
    else if (okA)
    {
        rec.serial = recA.serial + 1u;
        target = BSP_NVM_SLOT_B_ADDR;
    }
    else if (okB)
    {
        rec.serial = recB.serial + 1u;
        target = BSP_NVM_SLOT_A_ADDR;
    }
    else
    {
        rec.serial = 1u;
        target = BSP_NVM_SLOT_A_ADDR;
    }

    s = record_write(target, &rec);
    if (s)
        return s;

    return 0;
}