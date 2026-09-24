/********************************** (C) COPYRIGHT *******************************
 * CH591D BLE Night Light
 * - UART1 printf
 * - BLE light-control GATT service
 * - RTC timer, battery notification, flash backup
 *******************************************************************************/
#include "CONFIG.h"
#include "HAL.h"
#include "bsp_ble.h"
#include "bsp_led.h"
#include "bsp_rtc.h"
#include "bsp_battary.h"
#include "bsp_uart.h"


#include "broadcaster.h"

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

#if(defined(BLE_MAC)) && (BLE_MAC == TRUE)
const uint8_t MacAddr[6] = {0x84, 0xC2, 0xE4, 0x03, 0x02, 0x02};
#endif

__HIGH_CODE
__attribute__((noinline))
void Main_Circulation(void)
{
    while(1)
    {
            //     DelayMs(3000);
    //     PRINT("HELLO\r\n");
        TMOS_SystemProcess();
        BSP_BLE_TaskProcess();
    }
}

// int main(void)
// {

//     SetSysClock(CLK_SOURCE_PLL_60MHz);

//     DelayMs(2000);

//     /* UART1: used by printf via _write() in CH59x_sys.c */
//     BSP_UART_Init(115200);
//     PRINT("CH591D Night Light BLE start\r\n");

//     // /* Hardware BSP init */
//     // BSP_LED_Init();
//     // BSP_BAT_Init();

//     // /* BLE stack and HAL timers */
//     // CH59x_BLEInit();
//     // HAL_Init();
//     // BSP_RTC_Init();

//     // /* BLE GAP peripheral */
//     // GAPRole_PeripheralInit();
//     // BSP_BLE_Init();
//     PRINT("0\r\n");
//     CH59x_BLEInit();
//     HAL_Init();

//     PRINT("1\r\n");
//     GAPRole_BroadcasterInit();
//     PRINT("2\r\n");
//     Broadcaster_Init();

//     PRINT("3\r\n");
//     Main_Circulation();
// }

void led_preinit() {
    GPIOA_ResetBits(IO_RED | IO_BLUE);
    GPIOB_ResetBits(IO_GREEN);
    GPIOA_ModeCfg(IO_RED | IO_BLUE, GPIO_ModeOut_PP_5mA);
    GPIOB_ModeCfg(IO_GREEN, GPIO_ModeOut_PP_5mA);
}

int main(void)
{
#if(defined(DCDC_ENABLE)) && (DCDC_ENABLE == TRUE)
    PWR_DCDCCfg(ENABLE);
#endif

    SetSysClock(CLK_SOURCE_PLL_60MHz);

    led_preinit();
    /* UART1: used by printf via _write() in CH59x_sys.c */
    BSP_UART_Init(115200);
    PRINT("CH591D Night Light BLE start\r\n");

    /* Hardware BSP init */
    BSP_LED_Init();
    BSP_BAT_Init();

    /* BLE stack and HAL timers */
    CH59x_BLEInit();
    HAL_Init();
    BSP_RTC_Init();

    /* BLE GAP peripheral */
    GAPRole_PeripheralInit();
    BSP_BLE_Init();

    PRINT("Initialization done\r\n");
    Main_Circulation();
}