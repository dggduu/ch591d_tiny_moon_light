#include "CH59x_common.h"
#include "bsp_rtc.h"
#include "bsp_led.h"   /* BSP_LIGHT_BRIGHTNESS_LIMIT */

/* Up to 44 years after this epoch are supported by the HAL. */
#define BSP_RTC_BASE_YEAR       2020


#define BSP_RTC_SECS_PER_DAY    86400u
#define BSP_RTC_HALF_PERIOD     2u      /* RTC "cycle" counts 2 seconds */
#define BSP_RTC_CYC_PER_HALF    (CAB_LSIFQ * BSP_RTC_HALF_PERIOD)   /* 64000 @32K */

static BSP_RtcTimer_t g_timer;
static uint32_t g_lastTriggerMinute;

static uint32_t g_lastCycle;
static uint32_t g_cycleHigh;

/* Wall clock = g_offsetSec + cycles / CAB_LSIFQ */
static uint64_t g_offsetSec;

static bool is_leap(uint16_t y)
{
    return (bool)(((y % 4u) == 0u && (y % 100u) != 0u) || (y % 400u) == 0u);
}

static uint16_t days_in_month(uint16_t y, uint8_t m)
{
    switch (m)
    {
        case 1:  return 31;
        case 2:  return (uint16_t)(is_leap(y) ? 29 : 28);
        case 3:  return 31;
        case 4:  return 30;
        case 5:  return 31;
        case 6:  return 30;
        case 7:  return 31;
        case 8:  return 31;
        case 9:  return 30;
        case 10: return 31;
        case 11: return 30;
        case 12: return 31;
        default: return 30;
    }
}

/* days since 1970-01-01 (Howard Hinnant's days_from_civil) */
static int32_t days_from_civil(int32_t y, uint32_t m, uint32_t d)
{
    int32_t era;
    uint32_t yoe, doy, doe;

    y -= (m <= 2u) ? 1 : 0;
    era = ((y >= 0) ? y : (y - 399)) / 400;
    yoe = (uint32_t)(y - era * 400);
    doy = (153u * (m + ((m > 2u) ? (uint32_t)-3 : 9u)) + 2u) / 5u + d - 1u;
    doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

/* inverse of days_from_civil */
static void civil_from_days(int32_t z, uint16_t *py, uint8_t *pm, uint8_t *pd)
{
    int32_t era, y;
    uint32_t doe, yoe, doy, mp;

    z += 719468;
    era = ((z >= 0) ? z : (z - 146096)) / 146097;
    doe = (uint32_t)(z - era * 146097);
    yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    y = (int32_t)yoe + era * 400;
    doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    mp = (5u * doy + 2u) / 153u;
    *pd = (uint8_t)(doy - (153u * mp + 2u) / 5u + 1u);
    *pm = (uint8_t)(mp + ((mp < 10u) ? 3u : (uint32_t)-9));
    if (*pm <= 2u)
        y += 1;
    *py = (uint16_t)y;
}

/* Monday = 0 ... Sunday = 6 (Tomohiko Sakamoto) */
static uint8_t day_of_week_monday0(uint16_t y, uint8_t m, uint8_t d)
{
    static const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    uint32_t yy = (m < 3u) ? (uint32_t)(y - 1u) : (uint32_t)y;
    return (uint8_t)(((yy + yy / 4u - yy / 100u + yy / 400u + t[m - 1u] + d) % 7u + 6u) % 7u);
}

/* ------------------------------------------------------------------ */
/* Cycle counter bookkeeping                                          */
/* ------------------------------------------------------------------ */

static uint64_t cycles_now(void)
{
    uint32_t c = RTC_GetCycle32k();
    int32_t delta = (int32_t)(c - g_lastCycle);

    if (delta < 0)
        g_cycleHigh += RTC_MAX_COUNT;       /* counter wrapped around */

    g_lastCycle = c;
    return (uint64_t)g_cycleHigh + c;
}


static void clock_break_down(uint64_t cycles, uint16_t *py, uint8_t *pmo, uint8_t *pd,
                             uint8_t *ph, uint8_t *pmi, uint8_t *ps)
{
    uint64_t secs = g_offsetSec + (cycles / CAB_LSIFQ);
    int32_t days = (int32_t)(secs / BSP_RTC_SECS_PER_DAY);
    uint32_t rem = (uint32_t)(secs % BSP_RTC_SECS_PER_DAY);

    civil_from_days(days, py, pmo, pd);
    *ph = (uint8_t)(rem / 3600u);
    *pmi = (uint8_t)((rem % 3600u) / 60u);
    *ps = (uint8_t)(rem % 60u);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void BSP_RTC_Init(void)
{
    g_timer.enable = 0;
    g_timer.hour = 22;
    g_timer.minute = 0;
    g_timer.repeat = 0;
    g_timer.action = BSP_RTC_ACTION_OFF;
    g_timer.brightness = BSP_LIGHT_BRIGHTNESS_LIMIT;
    g_timer.rgb[0] = 255;
    g_timer.rgb[1] = 200;
    g_timer.rgb[2] = 150;
    g_timer.durationMin = 0;
    g_lastTriggerMinute = 0;

    g_cycleHigh = 0;
    g_lastCycle = RTC_GetCycle32k();

    /* The seeded RTC already carries a fraction of its 2 second cycle. */
    g_offsetSec = (uint64_t)days_from_civil((int32_t)BSP_RTC_BASE_YEAR, 1u, 1u)
                * BSP_RTC_SECS_PER_DAY;
}

void BSP_RTC_Tick(void)
{
    (void)cycles_now();
}

void BSP_RTC_SetWallClock(uint16_t year, uint8_t month, uint8_t day,
                          uint8_t hour, uint8_t minute, uint8_t second)
{
    uint16_t y = year;
    uint8_t mo = month;
    uint8_t d = day;
    uint64_t cycles = cycles_now();

    if (y < BSP_RTC_BASE_YEAR || y > (BSP_RTC_BASE_YEAR + 44))
        return;
    if (mo < 1u || mo > 12u)
        return;
    if (d < 1u || d > (uint8_t)days_in_month(y, mo))
        return;
    if (hour > 23u || minute > 59u || second > 59u)
        return;

    /* Pure software offset, the RTC counter is left alone (BLE keeps running). */
    g_offsetSec = (uint64_t)days_from_civil((int32_t)y, mo, d) * BSP_RTC_SECS_PER_DAY
                + (uint64_t)hour * 3600u
                + (uint64_t)minute * 60u
                + (uint64_t)second
                - (uint64_t)(cycles / CAB_LSIFQ);

}

void BSP_RTC_GetWallClock(uint16_t *year, uint8_t *month, uint8_t *day,
                          uint8_t *hour, uint8_t *minute, uint8_t *second)
{
    uint16_t y;
    uint8_t mo, d, h, mi, s;

    clock_break_down(cycles_now(), &y, &mo, &d, &h, &mi, &s);

    if (year)   *year = y;
    if (month)  *month = mo;
    if (day)    *day = d;
    if (hour)   *hour = h;
    if (minute) *minute = mi;
    if (second) *second = s;
}

void BSP_RTC_SetTimer(const BSP_RtcTimer_t *pTimer)
{
    if (pTimer)
    {
        g_timer = *pTimer;
    }
}

void BSP_RTC_GetTimer(BSP_RtcTimer_t *pOut)
{
    if (pOut)
    {
        *pOut = g_timer;
    }
}

void BSP_RTC_ClearTimer(void)
{
    g_timer.enable = 0;
}

/*********************************************************************
 * @fn      BSP_RTC_GetMinuteToken
 * @brief   Unique token for the current wall-clock minute
 * @note    year(12)|month(4)|day(5)|hour(5)|minute(6) = 32 bits
 *********************************************************************/
uint32_t BSP_RTC_GetMinuteToken(void)
{
    uint16_t y;
    uint8_t mo, d, h, mi, s;

    BSP_RTC_GetWallClock(&y, &mo, &d, &h, &mi, &s);

    return (((uint32_t)y & 0x0FFFu) << 20)
         | (((uint32_t)mo & 0x0Fu) << 16)
         | (((uint32_t)d  & 0x1Fu) << 11)
         | (((uint32_t)h  & 0x1Fu) <<  6)
         | ((uint32_t)mi  & 0x3Fu);
}

bool BSP_RTC_CheckTimer(void)
{
    uint16_t y;
    uint8_t mo, d, h, mi, s;
    uint32_t token;

    if (!g_timer.enable)
        return false;

    BSP_RTC_GetWallClock(&y, &mo, &d, &h, &mi, &s);

    if (h != g_timer.hour || mi != g_timer.minute)
        return false;

    token = BSP_RTC_GetMinuteToken();
    if (token == g_lastTriggerMinute)
        return false;

    if (g_timer.repeat != 0)
    {
        uint8_t bit = (uint8_t)(1u << day_of_week_monday0(y, mo, d));
        if ((g_timer.repeat & bit) == 0)
            return false;
    }

    g_lastTriggerMinute = token;
    return true;
}
