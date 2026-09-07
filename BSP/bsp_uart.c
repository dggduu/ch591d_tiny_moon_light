#include "bsp_uart.h"
#include "CH59x_gpio.h"

static uint32_t g_baudrate = BSP_UART_DEFAULT_BAUDRATE;

void BSP_UART_Init(uint32_t baudrate)
{
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);

    UART1_DefInit();
    if(baudrate != 115200)
        UART1_BaudRateCfg(baudrate);
    g_baudrate = baudrate;
}

void BSP_UART_DeInit(void)
{
    UART1_Reset();
}

void BSP_UART_SendByte(uint8_t data)
{
    UART1_SendByte(data);
}

void BSP_UART_SendString(const uint8_t *buf, uint16_t len)
{
    UART1_SendString((uint8_t *)buf, len);
}

uint8_t BSP_UART_RecvByte(void)
{
    return UART1_RecvByte();
}

uint16_t BSP_UART_RecvString(uint8_t *buf)
{
    return UART1_RecvString(buf);
}

uint8_t BSP_UART_RecvByteNonBlock(void)
{
    if(R8_UART1_RFC == 0)
        return 0xFF; /* no data */
    return UART1_RecvByte();
}

void BSP_UART_SetBaudrate(uint32_t baudrate)
{
    UART1_BaudRateCfg(baudrate);
    g_baudrate = baudrate;
}

uint32_t BSP_UART_GetBaudrate(void)
{
    return g_baudrate;
}
