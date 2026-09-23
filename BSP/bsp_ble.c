/********************************** (C) COPYRIGHT *******************************
 * BSP BLE peripheral and custom night-light GATT service
 *******************************************************************************/
#include "CONFIG.h"
#include "HAL.h"
#include "bsp_ble.h"
#include "bsp_led.h"
#include "bsp_rtc.h"
#include "bsp_flash.h"
#include "bsp_battary.h"
#include "bsp_uart.h"
#include "CH59xBLE_LIB.h"

#include <string.h>

#define BSP_BLE_START_DEVICE_EVT     0x0001
#define BSP_BLE_BATTERY_EVT          0x0002
#define BSP_BLE_TICK_EVT             0x0004
#define BSP_BLE_SAVE_EVT             0x0008

#define BSP_BLE_ADV_INTERVAL         160u
#define BSP_BLE_TICK_PERIOD_MS       50u
#define BSP_BLE_BATTERY_PERIOD_MS    30000u
#define BSP_BLE_SAVE_DELAY_MS        2000u

#define BSP_BLE_CHAR_TIMER_LEN       ((uint16_t)sizeof(BSP_RtcTimer_t))

typedef struct
{
    uint16_t connHandle;
    uint16_t connInterval;
    uint16_t connSlaveLatency;
    uint16_t connTimeout;
} BSP_BleConnItem_t;

static uint8_t BSP_BleTaskID = INVALID_TASK_ID;
static BSP_BleConnItem_t connItem;
static BSP_Nvm_t g_nvm;
static uint8_t g_nvmDirty = 0;

static uint8_t attDeviceName[] = "CH591D";

static uint8_t advertData[] = {
    0x02, GAP_ADTYPE_FLAGS,
    GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
    0x03, GAP_ADTYPE_16BIT_MORE,
    LO_UINT16(BSP_BLE_SERVICE_UUID), HI_UINT16(BSP_BLE_SERVICE_UUID)
};

static uint8_t scanRspData[] = {
    7, GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    'C','H','5','9','1','D',
};

static uint8_t valPower;
static uint8_t valBrightness;
static uint8_t valMode;
static uint8_t valColor[3];
static uint8_t valColorTemp[2];
static uint8_t valEffectSpeed;
static uint8_t valTimer[BSP_BLE_CHAR_TIMER_LEN];
static uint8_t valBattery;
static uint8_t valRtcTime[7];

static gattCharCfg_t batteryCharCfg[PERIPHERAL_MAX_CONNECTION];

static uint8_t propPower        = GATT_PROP_READ | GATT_PROP_WRITE;
static uint8_t propBrightness   = GATT_PROP_READ | GATT_PROP_WRITE;
static uint8_t propMode         = GATT_PROP_READ | GATT_PROP_WRITE;
static uint8_t propColor        = GATT_PROP_READ | GATT_PROP_WRITE;
static uint8_t propColorTemp    = GATT_PROP_READ | GATT_PROP_WRITE;
static uint8_t propEffectSpeed  = GATT_PROP_READ | GATT_PROP_WRITE;
static uint8_t propTimer        = GATT_PROP_READ | GATT_PROP_WRITE;
static uint8_t propBattery      = GATT_PROP_READ | GATT_PROP_NOTIFY;
static uint8_t propRtcTime      = GATT_PROP_READ | GATT_PROP_WRITE;

static const uint8_t servUUID[ATT_BT_UUID_SIZE]        = { LO_UINT16(BSP_BLE_SERVICE_UUID), HI_UINT16(BSP_BLE_SERVICE_UUID) };
static const uint8_t powerUUID[ATT_BT_UUID_SIZE]       = { LO_UINT16(BSP_BLE_POWER_UUID), HI_UINT16(BSP_BLE_POWER_UUID) };
static const uint8_t brightnessUUID[ATT_BT_UUID_SIZE]  = { LO_UINT16(BSP_BLE_BRIGHTNESS_UUID), HI_UINT16(BSP_BLE_BRIGHTNESS_UUID) };
static const uint8_t modeUUID[ATT_BT_UUID_SIZE]        = { LO_UINT16(BSP_BLE_MODE_UUID), HI_UINT16(BSP_BLE_MODE_UUID) };
static const uint8_t colorUUID[ATT_BT_UUID_SIZE]       = { LO_UINT16(BSP_BLE_COLOR_UUID), HI_UINT16(BSP_BLE_COLOR_UUID) };
static const uint8_t colorTempUUID[ATT_BT_UUID_SIZE]   = { LO_UINT16(BSP_BLE_COLOR_TEMP_UUID), HI_UINT16(BSP_BLE_COLOR_TEMP_UUID) };
static const uint8_t effectSpeedUUID[ATT_BT_UUID_SIZE] = { LO_UINT16(BSP_BLE_EFFECT_SPEED_UUID), HI_UINT16(BSP_BLE_EFFECT_SPEED_UUID) };
static const uint8_t timerUUID[ATT_BT_UUID_SIZE]       = { LO_UINT16(BSP_BLE_TIMER_UUID), HI_UINT16(BSP_BLE_TIMER_UUID) };
static const uint8_t batteryUUID[ATT_BT_UUID_SIZE]     = { LO_UINT16(BSP_BLE_BATTERY_UUID), HI_UINT16(BSP_BLE_BATTERY_UUID) };
static const uint8_t rtcTimeUUID[ATT_BT_UUID_SIZE]     = { LO_UINT16(BSP_BLE_RTC_TIME_UUID), HI_UINT16(BSP_BLE_RTC_TIME_UUID) };

static gattAttrType_t lightServiceType = {ATT_BT_UUID_SIZE, (uint8_t *)servUUID};

static gattAttribute_t lightAttrTbl[] = {
    { {ATT_BT_UUID_SIZE, primaryServiceUUID}, GATT_PERMIT_READ, 0, (uint8_t *)&lightServiceType },
    { {ATT_BT_UUID_SIZE, characterUUID},      GATT_PERMIT_READ, 0, &propPower },
    { {ATT_BT_UUID_SIZE, powerUUID},          GATT_PERMIT_READ | GATT_PERMIT_WRITE, 0, &valPower },
    { {ATT_BT_UUID_SIZE, characterUUID},      GATT_PERMIT_READ, 0, &propBrightness },
    { {ATT_BT_UUID_SIZE, brightnessUUID},     GATT_PERMIT_READ | GATT_PERMIT_WRITE, 0, &valBrightness },
    { {ATT_BT_UUID_SIZE, characterUUID},      GATT_PERMIT_READ, 0, &propMode },
    { {ATT_BT_UUID_SIZE, modeUUID},           GATT_PERMIT_READ | GATT_PERMIT_WRITE, 0, &valMode },
    { {ATT_BT_UUID_SIZE, characterUUID},      GATT_PERMIT_READ, 0, &propColor },
    { {ATT_BT_UUID_SIZE, colorUUID},          GATT_PERMIT_READ | GATT_PERMIT_WRITE, 0, valColor },
    { {ATT_BT_UUID_SIZE, characterUUID},      GATT_PERMIT_READ, 0, &propColorTemp },
    { {ATT_BT_UUID_SIZE, colorTempUUID},      GATT_PERMIT_READ | GATT_PERMIT_WRITE, 0, valColorTemp },
    { {ATT_BT_UUID_SIZE, characterUUID},      GATT_PERMIT_READ, 0, &propEffectSpeed },
    { {ATT_BT_UUID_SIZE, effectSpeedUUID},    GATT_PERMIT_READ | GATT_PERMIT_WRITE, 0, &valEffectSpeed },
    { {ATT_BT_UUID_SIZE, characterUUID},      GATT_PERMIT_READ, 0, &propTimer },
    { {ATT_BT_UUID_SIZE, timerUUID},          GATT_PERMIT_READ | GATT_PERMIT_WRITE, 0, valTimer },
    { {ATT_BT_UUID_SIZE, characterUUID},      GATT_PERMIT_READ, 0, &propBattery },
    { {ATT_BT_UUID_SIZE, batteryUUID},        GATT_PERMIT_READ, 0, &valBattery },
    { {ATT_BT_UUID_SIZE, clientCharCfgUUID},  GATT_PERMIT_READ | GATT_PERMIT_WRITE, 0, (uint8_t *)batteryCharCfg },
    { {ATT_BT_UUID_SIZE, characterUUID},      GATT_PERMIT_READ, 0, &propRtcTime },
    { {ATT_BT_UUID_SIZE, rtcTimeUUID},        GATT_PERMIT_READ | GATT_PERMIT_WRITE, 0, valRtcTime },
};

static bStatus_t BSP_LightReadAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
                                     uint8_t *pValue, uint16_t *pLen,
                                     uint16_t offset, uint16_t maxLen, uint8_t method);
static bStatus_t BSP_LightWriteAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
                                      uint8_t *pValue, uint16_t len,
                                      uint16_t offset, uint8_t method);
static void BSP_Ble_ProcessTMOSMsg(tmos_event_hdr_t *pMsg);
static void BSP_Ble_StateCB(gapRole_States_t newState, gapRoleEvent_t *pEvent);
static void BSP_Ble_LinkEstablished(gapRoleEvent_t *pEvent);
static void BSP_Ble_LinkTerminated(gapRoleEvent_t *pEvent);
static void BSP_Ble_RssiCB(uint16_t connHandle, int8_t rssi);
static void BSP_Ble_ParamUpdateCB(uint16_t connHandle, uint16_t connInterval,
                                  uint16_t connSlaveLatency, uint16_t connTimeout);
static uint16_t BSP_Ble_ProcessEvent(uint8_t task_id, uint16_t events);

static gattServiceCBs_t lightServiceCBs = {
    BSP_LightReadAttrCB,
    BSP_LightWriteAttrCB,
    NULL
};

static gapRolesCBs_t BSP_Ble_PeripheralCBs = {
    BSP_Ble_StateCB,
    BSP_Ble_RssiCB,
    BSP_Ble_ParamUpdateCB
};

static gapBondCBs_t BSP_Ble_BondCBs = {
    NULL,
    NULL
};

/* ============================================================
 *  NVM 防抖
 * ============================================================ */
static void save_settings(void)
{
    g_nvm.light.power = valPower;
    g_nvm.light.brightness = valBrightness;
    g_nvm.light.mode = valMode;
    g_nvm.light.rgb[0] = valColor[0];
    g_nvm.light.rgb[1] = valColor[1];
    g_nvm.light.rgb[2] = valColor[2];
    g_nvm.light.colorTemp = (uint16_t)(valColorTemp[0] | ((uint16_t)valColorTemp[1] << 8));
    g_nvm.light.effectSpeed = valEffectSpeed;

    BSP_RtcTimer_t timer;
    BSP_RTC_GetTimer(&timer);
    g_nvm.timer = timer;

    BSP_NVM_Save(&g_nvm);
}

static void mark_nvm_dirty(void)
{
    g_nvmDirty = 1;
    if (BSP_BleTaskID != INVALID_TASK_ID)
    {
        tmos_stop_task(BSP_BleTaskID, BSP_BLE_SAVE_EVT);
        tmos_start_task(BSP_BleTaskID, BSP_BLE_SAVE_EVT,
                        MS1_TO_SYSTEM_TIME(BSP_BLE_SAVE_DELAY_MS));
    }
}

static void flush_nvm(void)
{
    if (g_nvmDirty)
    {
        save_settings();
        g_nvmDirty = 0;
    }
}

/* ============================================================
 *  运行时查找属性句柄
 * ============================================================ */
static uint16_t find_attr_handle(uint16_t uuid)
{
    uint8_t i;
    for (i = 0; i < GATT_NUM_ATTRS(lightAttrTbl); i++)
    {
        if (lightAttrTbl[i].type.len == ATT_BT_UUID_SIZE)
        {
            uint16_t u = BUILD_UINT16(lightAttrTbl[i].type.uuid[0],
                                      lightAttrTbl[i].type.uuid[1]);
            if (u == uuid)
                return lightAttrTbl[i].handle;
        }
    }
    return 0;
}

/* ============================================================
 *  加载与应用
 * ============================================================ */
static void load_settings(void)
{
    BSP_LightState_t light;
    BSP_RtcTimer_t timer;

    if (BSP_NVM_Load(&g_nvm) != 0)
        BSP_NVM_LoadDefault(&g_nvm);

    light = g_nvm.light;

    /* 【防御】即使 Flash 里存着 100，也要钳到 70 */
    if (light.brightness > BSP_LIGHT_BRIGHTNESS_LIMIT)
        light.brightness = BSP_LIGHT_BRIGHTNESS_LIMIT;

    valPower = light.power;
    valBrightness = light.brightness;
    valMode = light.mode;
    valColor[0] = light.rgb[0];
    valColor[1] = light.rgb[1];
    valColor[2] = light.rgb[2];
    valColorTemp[0] = light.colorTemp & 0xFF;
    valColorTemp[1] = (light.colorTemp >> 8) & 0xFF;
    valEffectSpeed = light.effectSpeed;

    BSP_LED_SetState(&light);

    timer = g_nvm.timer;
    if (timer.brightness > BSP_LIGHT_BRIGHTNESS_LIMIT)
        timer.brightness = BSP_LIGHT_BRIGHTNESS_LIMIT;

    BSP_RTC_SetTimer(&timer);
    memcpy(valTimer, &timer, sizeof(timer));
}

static void apply_timer_to_light(const BSP_RtcTimer_t *pTimer)
{
    BSP_LightState_t light;
    BSP_LED_GetState(&light);

    if (pTimer->action == BSP_RTC_ACTION_OFF)
    {
        light.power = 0;
        BSP_LED_SetState(&light);
        valPower = 0;
    }
    else if (pTimer->action == BSP_RTC_ACTION_ON || pTimer->action == BSP_RTC_ACTION_SET)
    {
        light.power = 1;
        light.brightness = pTimer->brightness;   /* BSP_LED_SetState 内部会钳位 */
        light.rgb[0] = pTimer->rgb[0];
        light.rgb[1] = pTimer->rgb[1];
        light.rgb[2] = pTimer->rgb[2];
        BSP_LED_SetState(&light);

        valPower = 1;
        valBrightness = light.brightness;
        valMode = light.mode;
        valColor[0] = light.rgb[0];
        valColor[1] = light.rgb[1];
        valColor[2] = light.rgb[2];
    }
}

static void read_wall_clock_into_attr(void)
{
    uint16_t y;
    uint8_t mo, d, h, mi, s;
    BSP_RTC_GetWallClock(&y, &mo, &d, &h, &mi, &s);
    valRtcTime[0] = y & 0xFF;
    valRtcTime[1] = (y >> 8) & 0xFF;
    valRtcTime[2] = mo;
    valRtcTime[3] = d;
    valRtcTime[4] = h;
    valRtcTime[5] = mi;
    valRtcTime[6] = s;
}

/* ============================================================
 *  GATT 读回调
 * ============================================================ */
static bStatus_t BSP_LightReadAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
                                     uint8_t *pValue, uint16_t *pLen,
                                     uint16_t offset, uint16_t maxLen, uint8_t method)
{
    bStatus_t status = SUCCESS;
    uint16_t uuid;

    if (offset > 0)
        return ATT_ERR_ATTR_NOT_LONG;

    if (pAttr->type.len != ATT_BT_UUID_SIZE)
    {
        *pLen = 0;
        return ATT_ERR_INVALID_HANDLE;
    }

    uuid = BUILD_UINT16(pAttr->type.uuid[0], pAttr->type.uuid[1]);

    if (uuid == BSP_BLE_RTC_TIME_UUID)
    {
        read_wall_clock_into_attr();
        *pLen = sizeof(valRtcTime);
        if (*pLen > maxLen) *pLen = maxLen;
        tmos_memcpy(pValue, valRtcTime, *pLen);
        return SUCCESS;
    }

    if (uuid == BSP_BLE_BATTERY_UUID)
    {
        valBattery = BSP_BAT_GetLevel();
    }

    switch (uuid)
    {
        case BSP_BLE_POWER_UUID:
        case BSP_BLE_BRIGHTNESS_UUID:
        case BSP_BLE_MODE_UUID:
        case BSP_BLE_EFFECT_SPEED_UUID:
            *pLen = 1;
            if (*pLen > maxLen) *pLen = maxLen;
            tmos_memcpy(pValue, pAttr->pValue, *pLen);
            break;

        case BSP_BLE_COLOR_UUID:
            *pLen = 3;
            if (*pLen > maxLen) *pLen = maxLen;
            tmos_memcpy(pValue, pAttr->pValue, *pLen);
            break;

        case BSP_BLE_COLOR_TEMP_UUID:
            *pLen = 2;
            if (*pLen > maxLen) *pLen = maxLen;
            tmos_memcpy(pValue, pAttr->pValue, *pLen);
            break;

        case BSP_BLE_TIMER_UUID:
            *pLen = BSP_BLE_CHAR_TIMER_LEN;
            if (*pLen > maxLen) *pLen = maxLen;
            tmos_memcpy(pValue, pAttr->pValue, *pLen);
            break;

        case BSP_BLE_BATTERY_UUID:
            *pLen = 1;
            if (*pLen > maxLen) *pLen = maxLen;
            tmos_memcpy(pValue, pAttr->pValue, *pLen);
            break;

        default:
            *pLen = 0;
            status = ATT_ERR_ATTR_NOT_FOUND;
            break;
    }
    return status;
}

/* ============================================================
 *  GATT 写回调
 * ============================================================ */
static bStatus_t BSP_LightWriteAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
                                      uint8_t *pValue, uint16_t len,
                                      uint16_t offset, uint8_t method)
{
    bStatus_t status = SUCCESS;
    uint16_t uuid;
    BSP_LightState_t light;

    if (offset > 0)
        return ATT_ERR_ATTR_NOT_LONG;

    if (pAttr->type.len != ATT_BT_UUID_SIZE)
        return ATT_ERR_INVALID_HANDLE;

    uuid = BUILD_UINT16(pAttr->type.uuid[0], pAttr->type.uuid[1]);

    if (uuid == GATT_CLIENT_CHAR_CFG_UUID)
    {
        return GATTServApp_ProcessCCCWriteReq(connHandle, pAttr, pValue, len,
                                              offset, GATT_CLIENT_CFG_NOTIFY);
    }

    BSP_LED_GetState(&light);
    switch (uuid)
    {
        case BSP_BLE_POWER_UUID:
            if (len != 1 || pValue[0] > 1)
                status = ATT_ERR_INVALID_VALUE_SIZE;
            else
            {
                light.power = pValue[0];
                BSP_LED_SetState(&light);
                valPower = pValue[0];
            }
            break;

        case BSP_BLE_BRIGHTNESS_UUID:
            /* 【修改】上限改为 BSP_LIGHT_BRIGHTNESS_LIMIT(70) */
            if (len != 1 || pValue[0] > BSP_LIGHT_BRIGHTNESS_LIMIT)
                status = ATT_ERR_INVALID_VALUE_SIZE;
            else
            {
                light.brightness = pValue[0];
                BSP_LED_SetState(&light);
                valBrightness = pValue[0];
            }
            break;

        case BSP_BLE_MODE_UUID:
            if (len != 1 || pValue[0] > BSP_LIGHT_MODE_COLORFUL)
                status = ATT_ERR_INVALID_VALUE_SIZE;
            else
            {
                light.mode = pValue[0];
                BSP_LED_SetState(&light);
                valMode = pValue[0];
            }
            break;

        case BSP_BLE_COLOR_UUID:
            if (len != 3)
                status = ATT_ERR_INVALID_VALUE_SIZE;
            else
            {
                light.rgb[0] = pValue[0];
                light.rgb[1] = pValue[1];
                light.rgb[2] = pValue[2];
                BSP_LED_SetState(&light);
                valColor[0] = pValue[0];
                valColor[1] = pValue[1];
                valColor[2] = pValue[2];
            }
            break;

        case BSP_BLE_COLOR_TEMP_UUID:
            if (len != 2)
                status = ATT_ERR_INVALID_VALUE_SIZE;
            else
            {
                uint16_t ct = (uint16_t)(pValue[0] | ((uint16_t)pValue[1] << 8));
                if (ct < 2700) ct = 2700;
                if (ct > 6500) ct = 6500;
                light.colorTemp = ct;
                BSP_LED_SetState(&light);
                valColorTemp[0] = ct & 0xFF;
                valColorTemp[1] = (ct >> 8) & 0xFF;
            }
            break;

        case BSP_BLE_EFFECT_SPEED_UUID:
            if (len != 1 || pValue[0] > 100)
                status = ATT_ERR_INVALID_VALUE_SIZE;
            else
            {
                light.effectSpeed = pValue[0];
                BSP_LED_SetState(&light);
                valEffectSpeed = pValue[0];
            }
            break;

        case BSP_BLE_TIMER_UUID:
            if (len != BSP_BLE_CHAR_TIMER_LEN)
            {
                status = ATT_ERR_INVALID_VALUE_SIZE;
            }
            else
            {
                BSP_RtcTimer_t newTimer;
                memcpy(&newTimer, pValue, sizeof(newTimer));
                /* 【修改】亮度上限 70；同时校验 enable / repeat */
                if (newTimer.enable > 1 ||
                    newTimer.hour > 23 || newTimer.minute > 59 ||
                    newTimer.brightness > BSP_LIGHT_BRIGHTNESS_LIMIT ||
                    newTimer.action > BSP_RTC_ACTION_SET ||
                    newTimer.repeat > 0x7F)
                {
                    status = ATT_ERR_INVALID_VALUE_SIZE;
                }
                else
                {
                    BSP_RTC_SetTimer(&newTimer);
                    memcpy(valTimer, pValue, sizeof(newTimer));
                }
            }
            break;

        case BSP_BLE_RTC_TIME_UUID:
            if (len != 7)
                status = ATT_ERR_INVALID_VALUE_SIZE;
            else
            {
                uint16_t year = (uint16_t)(pValue[0] | ((uint16_t)pValue[1] << 8));
                BSP_RTC_SetWallClock(year, pValue[2], pValue[3],
                                     pValue[4], pValue[5], pValue[6]);
                memcpy(valRtcTime, pValue, 7);
            }
            break;

        default:
            status = ATT_ERR_ATTR_NOT_FOUND;
            break;
    }

    if (status == SUCCESS)
    {
        if (uuid != BSP_BLE_RTC_TIME_UUID)
            mark_nvm_dirty();
    }
    return status;
}

/* ============================================================
 *  电池通知
 * ============================================================ */
static void BSP_Ble_NotifyBattery(void)
{
    attHandleValueNoti_t noti;
    uint16_t cfg;
    uint16_t handle;

    if (connItem.connHandle == GAP_CONNHANDLE_INIT)
        return;

    cfg = GATTServApp_ReadCharCfg(connItem.connHandle, batteryCharCfg);
    if (!(cfg & GATT_CLIENT_CFG_NOTIFY))
        return;

    handle = find_attr_handle(BSP_BLE_BATTERY_UUID);
    if (handle == 0)
        return;

    valBattery = BSP_BAT_GetLevel();
    noti.handle = handle;
    noti.len = 1;
    noti.pValue = &valBattery;
    GATT_Notification(connItem.connHandle, &noti, FALSE);
}

/* ============================================================
 *  周期任务
 * ============================================================ */
static void BSP_Ble_Tick(void)
{
    static uint32_t rtcMsAccum = 0;
    static uint32_t autoOffRemainSec = 0;

    BSP_LED_TaskTick();

    rtcMsAccum += BSP_BLE_TICK_PERIOD_MS;
    if (rtcMsAccum >= 1000u)
    {
        BSP_RtcTimer_t timer;
        rtcMsAccum -= 1000u;

        if (autoOffRemainSec > 0)
        {
            autoOffRemainSec--;
            if (autoOffRemainSec == 0)
            {
                BSP_LightState_t light;
                BSP_LED_GetState(&light);
                light.power = 0;
                BSP_LED_SetState(&light);
                valPower = 0;
                mark_nvm_dirty();
            }
        }

        if (BSP_RTC_CheckTimer())
        {
            BSP_RTC_GetTimer(&timer);
            apply_timer_to_light(&timer);

            if (timer.action != BSP_RTC_ACTION_OFF && timer.durationMin > 0)
                autoOffRemainSec = (uint32_t)timer.durationMin * 60u;
            else if (timer.action == BSP_RTC_ACTION_OFF)
                autoOffRemainSec = 0;

            if (timer.repeat == 0)
            {
                BSP_RTC_ClearTimer();
                timer.enable = 0;
                memcpy(valTimer, &timer, sizeof(timer));
            }

            save_settings();
        }
    }
}

/* ============================================================
 *  服务注册 / 事件处理
 * ============================================================ */
static bStatus_t BSP_Ble_AddLightService(void)
{
    bStatus_t status;
    GATTServApp_InitCharCfg(INVALID_CONNHANDLE, batteryCharCfg);
    status = GATTServApp_RegisterService(lightAttrTbl,
                                         GATT_NUM_ATTRS(lightAttrTbl),
                                         GATT_MAX_ENCRYPT_KEY_SIZE,
                                         &lightServiceCBs);
    return status;
}

static uint16_t BSP_Ble_ProcessEvent(uint8_t task_id, uint16_t events)
{
    if (events & SYS_EVENT_MSG)
    {
        uint8_t *pMsg;
        if ((pMsg = tmos_msg_receive(BSP_BleTaskID)) != NULL)
        {
            BSP_Ble_ProcessTMOSMsg((tmos_event_hdr_t *)pMsg);
            tmos_msg_deallocate(pMsg);
        }
        return (events ^ SYS_EVENT_MSG);
    }

    if (events & BSP_BLE_START_DEVICE_EVT)
    {
        GAPRole_PeripheralStartDevice(BSP_BleTaskID, &BSP_Ble_BondCBs, &BSP_Ble_PeripheralCBs);
        return (events ^ BSP_BLE_START_DEVICE_EVT);
    }

    if (events & BSP_BLE_TICK_EVT)
    {
        BSP_Ble_Tick();
        tmos_start_task(BSP_BleTaskID, BSP_BLE_TICK_EVT,
                        MS1_TO_SYSTEM_TIME(BSP_BLE_TICK_PERIOD_MS));
        return (events ^ BSP_BLE_TICK_EVT);
    }

    if (events & BSP_BLE_BATTERY_EVT)
    {
        BSP_Ble_NotifyBattery();
        tmos_start_task(BSP_BleTaskID, BSP_BLE_BATTERY_EVT,
                        MS1_TO_SYSTEM_TIME(BSP_BLE_BATTERY_PERIOD_MS));
        return (events ^ BSP_BLE_BATTERY_EVT);
    }

    if (events & BSP_BLE_SAVE_EVT)
    {
        flush_nvm();
        return (events ^ BSP_BLE_SAVE_EVT);
    }

    return 0;
}

static void BSP_Ble_ProcessTMOSMsg(tmos_event_hdr_t *pMsg)
{
    (void)pMsg;
}

void BSP_BLE_Init(void)
{
    uint8_t advertising_enable = TRUE;
    uint16_t desired_min_interval = 6;
    uint16_t desired_max_interval = 1000;

    connItem.connHandle = GAP_CONNHANDLE_INIT;
    connItem.connInterval = 0;
    connItem.connSlaveLatency = 0;
    connItem.connTimeout = 0;

    BSP_BleTaskID = TMOS_ProcessEventRegister(BSP_Ble_ProcessEvent);

    load_settings();

    GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &advertising_enable);
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);
    GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData);
    GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL, sizeof(uint16_t), &desired_min_interval);
    GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL, sizeof(uint16_t), &desired_max_interval);

    {
        uint16_t advInt = BSP_BLE_ADV_INTERVAL;
        GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN, advInt);
        GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX, advInt);
    }

    GGS_AddService(GATT_ALL_SERVICES);
    GATTServApp_AddService(GATT_ALL_SERVICES);
    BSP_Ble_AddLightService();
    GGS_SetParameter(GGS_DEVICE_NAME_ATT, sizeof(attDeviceName) - 1, attDeviceName);

    valBattery = BSP_BAT_GetLevel();
    read_wall_clock_into_attr();

    tmos_set_event(BSP_BleTaskID, BSP_BLE_START_DEVICE_EVT);
    tmos_start_task(BSP_BleTaskID, BSP_BLE_TICK_EVT,
                    MS1_TO_SYSTEM_TIME(BSP_BLE_TICK_PERIOD_MS));
    tmos_start_task(BSP_BleTaskID, BSP_BLE_BATTERY_EVT,
                    MS1_TO_SYSTEM_TIME(BSP_BLE_BATTERY_PERIOD_MS));
}

void BSP_BLE_TaskProcess(void)
{
}

static void BSP_Ble_StateCB(gapRole_States_t newState, gapRoleEvent_t *pEvent)
{
    switch (newState & GAPROLE_STATE_ADV_MASK)
    {
        case GAPROLE_STARTED:
            PRINT("BLE stack started\r\n");
            break;

        case GAPROLE_ADVERTISING:
            if (pEvent->gap.opcode == GAP_LINK_TERMINATED_EVENT)
                BSP_Ble_LinkTerminated(pEvent);
            break;

        case GAPROLE_CONNECTED:
            if (pEvent->gap.opcode == GAP_LINK_ESTABLISHED_EVENT)
                BSP_Ble_LinkEstablished(pEvent);
            break;

        case GAPROLE_WAITING:
            if (pEvent->gap.opcode == GAP_LINK_TERMINATED_EVENT)
                BSP_Ble_LinkTerminated(pEvent);
            break;

        default:
            break;
    }
}

static void BSP_Ble_LinkEstablished(gapRoleEvent_t *pEvent)
{
    gapEstLinkReqEvent_t *event = (gapEstLinkReqEvent_t *)pEvent;
    if (connItem.connHandle != GAP_CONNHANDLE_INIT)
    {
        GAPRole_TerminateLink(pEvent->linkCmpl.connectionHandle);
    }
    else
    {
        connItem.connHandle = event->connectionHandle;
        connItem.connInterval = event->connInterval;
        connItem.connSlaveLatency = event->connLatency;
        connItem.connTimeout = event->connTimeout;
        PRINT("BLE connected, handle=0x%04x\r\n", event->connectionHandle);
        BSP_Ble_NotifyBattery();
    }
}

static void BSP_Ble_LinkTerminated(gapRoleEvent_t *pEvent)
{
    gapTerminateLinkEvent_t *event = (gapTerminateLinkEvent_t *)pEvent;
    if (event->connectionHandle == connItem.connHandle)
    {
        flush_nvm();

        connItem.connHandle = GAP_CONNHANDLE_INIT;
        connItem.connInterval = 0;
        connItem.connSlaveLatency = 0;
        connItem.connTimeout = 0;

        {
            uint8_t advertising_enable = TRUE;
            GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &advertising_enable);
        }
    }
}

static void BSP_Ble_RssiCB(uint16_t connHandle, int8_t rssi)
{
    (void)connHandle;
    (void)rssi;
}

static void BSP_Ble_ParamUpdateCB(uint16_t connHandle, uint16_t connInterval,
                                  uint16_t connSlaveLatency, uint16_t connTimeout)
{
    if (connHandle == connItem.connHandle)
    {
        connItem.connInterval = connInterval;
        connItem.connSlaveLatency = connSlaveLatency;
        connItem.connTimeout = connTimeout;
    }
}