#ifndef __BSP_UART_H__
#define __BSP_UART_H__

#include "CH59x_common.h"
#include <stdio.h>   // for printf

/* 默认波特率 */
#define BSP_UART_DEFAULT_BAUDRATE  115200

/* 函数声明 */
void BSP_UART_Init(uint32_t baudrate);          // 初始化UART1（默认引脚RX_PA8/TX_PA9）
void BSP_UART_DeInit(void);                     // 关闭UART0

void BSP_UART_SendByte(uint8_t data);
void BSP_UART_SendString(const uint8_t *buf, uint16_t len);
uint8_t BSP_UART_RecvByte(void);
uint16_t BSP_UART_RecvString(uint8_t *buf);     // 读取FIFO中所有数据，返回长度
uint8_t BSP_UART_RecvByteNonBlock(void);


void BSP_UART_SetBaudrate(uint32_t baudrate);   // 动态修改波特率
uint32_t BSP_UART_GetBaudrate(void);            // 获取当前波特率（近似值）

#endif