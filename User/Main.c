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
        TMOS_SystemProcess();
        BSP_BLE_TaskProcess();
    }
}


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
    BSP_UART_Init(115200);
    PRINT("CH591D Night Light BLE start\r\n");

    BSP_LED_Init();
    BSP_BAT_Init();

    CH59x_BLEInit();
    HAL_Init();
    BSP_RTC_Init();

    GAPRole_PeripheralInit();
    BSP_BLE_Init();

    PRINT("Initialization done\r\n");
    Main_Circulation();
}