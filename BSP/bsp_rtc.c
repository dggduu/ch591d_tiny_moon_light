#include "CH59x_common.h"
#include "bsp_rtc.h"

static BSP_RtcTimer_t g_timer;
static uint32_t g_lastTriggerMinute;

/* Day of week, Monday=0 ... Sunday=6 */
static uint8_t day_of_week_monday0(uint16_t y, uint8_t m, uint8_t d)
{
    /* Tomohiko Sakamoto's algorithm, returns Sunday=0 */
    static const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    uint16_t yy = y;
    if(m < 3)
        yy -= 1;
    return (uint8_t)((yy + yy / 4 - yy / 100 + yy / 400 + t[m - 1] + d) % 7u + 6u) % 7u;
}

void BSP_RTC_Init(void)
{
    uint16_t y = 2020;
    uint8_t m = 1, d = 1, h = 0, mi = 0, s = 0;
    BSP_RTC_GetWallClock(&y, &m, &d, &h, &mi, &s); /* just reads current */
    g_timer.enable = 0;
    g_timer.hour = 22;
    g_timer.minute = 0;
    g_timer.repeat = 0;
    g_timer.action = BSP_RTC_ACTION_OFF;
    g_timer.brightness = 80;
    g_timer.rgb[0] = 255;
    g_timer.rgb[1] = 200;
    g_timer.rgb[2] = 150;
    g_timer.durationMin = 0;
    g_lastTriggerMinute = 0;
}

void BSP_RTC_SetWallClock(uint16_t year, uint8_t month, uint8_t day,
                          uint8_t hour, uint8_t minute, uint8_t second)
{
    if(year < 2000) year = 2020;
    if(month < 1 || month > 12) month = 1;
    if(day < 1 || day > 31) day = 1;
    if(hour > 23) hour = 0;
    if(minute > 59) minute = 0;
    if(second > 59) second = 0;
    RTC_InitTime(year, month, day, hour, minute, second);
}

void BSP_RTC_GetWallClock(uint16_t *year, uint8_t *month, uint8_t *day,
                          uint8_t *hour, uint8_t *minute, uint8_t *second)
{
    uint16_t y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    RTC_GetTime(&y, &mo, &d, &h, &mi, &s);
    if(year)   *year = y;
    if(month)  *month = (uint8_t)mo;
    if(day)    *day = (uint8_t)d;
    if(hour)   *hour = (uint8_t)h;
    if(minute) *minute = (uint8_t)mi;
    if(second) *second = (uint8_t)s;
}

void BSP_RTC_SetTimer(const BSP_RtcTimer_t *pTimer)
{
    if(pTimer)
        g_timer = *pTimer;
}

void BSP_RTC_GetTimer(BSP_RtcTimer_t *pOut)
{
    if(pOut)
        *pOut = g_timer;
}

void BSP_RTC_ClearTimer(void)
{
    g_timer.enable = 0;
}

uint32_t BSP_RTC_GetMinuteToken(void)
{
    uint16_t y;
    uint8_t mo, d, h, mi, s;
    BSP_RTC_GetWallClock(&y, &mo, &d, &h, &mi, &s);
    return (uint32_t)y * 1000000u + (uint32_t)mo * 10000u +
           (uint32_t)d * 100u + (uint32_t)h * 60u + mi;
}

bool BSP_RTC_CheckTimer(void)
{
    uint16_t y;
    uint8_t mo, d, h, mi, s;
    uint32_t token;

    if(!g_timer.enable)
        return false;

    BSP_RTC_GetWallClock(&y, &mo, &d, &h, &mi, &s);
    if(h != g_timer.hour || mi != g_timer.minute)
        return false;

    token = BSP_RTC_GetMinuteToken();
    if(token == g_lastTriggerMinute)
        return false;

    if(g_timer.repeat != 0)
    {
        uint8_t dow = day_of_week_monday0(y, mo, d);
        uint8_t bit = (uint8_t)(1u << dow);
        if((g_timer.repeat & bit) == 0)
            return false;
    }

    g_lastTriggerMinute = token;
    return true;
}
