/*********************************************************************
 * @file    bsp_rtc.c
 * @brief   RTC 定时器模块（支持单次/按星期重复触发）
 *********************************************************************/

#include "CH59x_common.h"
#include "bsp_rtc.h"
#include "bsp_led.h"
static BSP_RtcTimer_t g_timer;
static uint32_t g_lastTriggerMinute;

/* 星期计算（周一=0 ... 周日=6），Tomohiko Sakamoto 算法 */
static uint8_t day_of_week_monday0(uint16_t y, uint8_t m, uint8_t d)
{
    static const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    uint16_t yy = y;
    if (m < 3) yy -= 1;
    return (uint8_t)(((yy + yy / 4 - yy / 100 + yy / 400 + t[m - 1] + d) % 7u + 6u) % 7u);
}

void BSP_RTC_Init(void)
{
    g_timer.enable = 0;
    g_timer.hour = 22;
    g_timer.minute = 0;
    g_timer.repeat = 0;
    g_timer.action = BSP_RTC_ACTION_OFF;
    g_timer.brightness = BSP_LIGHT_BRIGHTNESS_LIMIT;   /* 默认 70 */
    g_timer.rgb[0] = 255;
    g_timer.rgb[1] = 200;
    g_timer.rgb[2] = 150;
    g_timer.durationMin = 0;
    g_lastTriggerMinute = 0;
}

void BSP_RTC_SetWallClock(uint16_t year, uint8_t month, uint8_t day,
                          uint8_t hour, uint8_t minute, uint8_t second)
{
    if (year < 2000) year = 2020;
    if (month < 1 || month > 12) month = 1;
    if (day < 1 || day > 31) day = 1;
    if (hour > 23) hour = 0;
    if (minute > 59) minute = 0;
    if (second > 59) second = 0;
    RTC_InitTime(year, month, day, hour, minute, second);
}

void BSP_RTC_GetWallClock(uint16_t *year, uint8_t *month, uint8_t *day,
                          uint8_t *hour, uint8_t *minute, uint8_t *second)
{
    uint16_t y, mo, d, h, mi, s;
    RTC_GetTime(&y, &mo, &d, &h, &mi, &s);
    if (year)   *year = y;
    if (month)  *month = (uint8_t)mo;
    if (day)    *day = (uint8_t)d;
    if (hour)   *hour = (uint8_t)h;
    if (minute) *minute = (uint8_t)mi;
    if (second) *second = (uint8_t)s;
}

void BSP_RTC_SetTimer(const BSP_RtcTimer_t *pTimer)
{
    if (pTimer) {
        g_timer = *pTimer;
    }
}

void BSP_RTC_GetTimer(BSP_RtcTimer_t *pOut)
{
    if (pOut) {
        *pOut = g_timer;
    }
}

void BSP_RTC_ClearTimer(void)
{
    g_timer.enable = 0;
}

/*********************************************************************
 * @fn      BSP_RTC_GetMinuteToken
 * @brief   生成当前时刻的分钟级唯一令牌
 * @note    位打包：年(12)|月(4)|日(5)|时(5)|分(6) = 32 位
 *********************************************************************/
uint32_t BSP_RTC_GetMinuteToken(void)
{
    uint16_t y;
    uint8_t mo, d, h, mi, s;
    BSP_RTC_GetWallClock(&y, &mo, &d, &h, &mi, &s);
    return ((uint32_t)y  << 20)
         | ((uint32_t)mo << 16)
         | ((uint32_t)d  << 11)
         | ((uint32_t)h  <<  6)
         | ((uint32_t)mi);
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

    if (g_timer.repeat != 0) {
        uint8_t dow = day_of_week_monday0(y, mo, d);
        uint8_t bit = (uint8_t)(1u << dow);
        if ((g_timer.repeat & bit) == 0)
            return false;
    }

    g_lastTriggerMinute = token;
    return true;
}