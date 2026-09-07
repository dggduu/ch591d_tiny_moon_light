#ifndef __BSP_RTC_H__
#define __BSP_RTC_H__

#include <stdint.h>
#include <stdbool.h>

/*
 * Repeat bits: bit0 = Monday ... bit6 = Sunday.
 * 0 means one-shot timer.
 */
#define BSP_RTC_REPEAT_MON   0x01u
#define BSP_RTC_REPEAT_TUE   0x02u
#define BSP_RTC_REPEAT_WED   0x04u
#define BSP_RTC_REPEAT_THU   0x08u
#define BSP_RTC_REPEAT_FRI   0x10u
#define BSP_RTC_REPEAT_SAT   0x20u
#define BSP_RTC_REPEAT_SUN   0x40u

/* Timer action */
#define BSP_RTC_ACTION_OFF   0u
#define BSP_RTC_ACTION_ON    1u
#define BSP_RTC_ACTION_SET   2u   /* apply brightness/color from this timer */

typedef struct __attribute__((packed))
{
    uint8_t enable;       /* 0=disable, 1=enable */
    uint8_t hour;         /* 0..23 */
    uint8_t minute;       /* 0..59 */
    uint8_t repeat;       /* see BSP_RTC_REPEAT_xxx; 0 = one-shot */
    uint8_t action;       /* off/on/set */
    uint8_t brightness;   /* 0..100 */
    uint8_t rgb[3];       /* R,G,B */
    uint8_t durationMin;  /* duration in minutes; 0 = no auto restore */
} BSP_RtcTimer_t;

void    BSP_RTC_Init(void);
void    BSP_RTC_SetWallClock(uint16_t year, uint8_t month, uint8_t day,
                             uint8_t hour, uint8_t minute, uint8_t second);
void    BSP_RTC_GetWallClock(uint16_t *year, uint8_t *month, uint8_t *day,
                             uint8_t *hour, uint8_t *minute, uint8_t *second);
void    BSP_RTC_SetTimer(const BSP_RtcTimer_t *pTimer);
void    BSP_RTC_GetTimer(BSP_RtcTimer_t *pOut);
void    BSP_RTC_ClearTimer(void);

/*
 * Called from a periodic task (e.g. every second).
 * Returns true only once per wall-clock minute when the timer time matches.
 */
bool    BSP_RTC_CheckTimer(void);

/* Get the wall-clock minute token used to suppress re-triggering */
uint32_t BSP_RTC_GetMinuteToken(void);

#endif /* __BSP_RTC_H__ */
