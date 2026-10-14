#include "CH59x_common.h"
#include "bsp_battary.h"

/* 分压比：BAT_ADC = VBAT × 82/(100+82) ≈ VBAT × 0.4505 */
/* 增益 0dB(1x)：ADC 端满量程 0~2.1V，4.2V 电池 -> 1.89V，饱和点约 VBAT 4.66V */
#define BAT_DIV_R1_K             100u
#define BAT_DIV_R2_K             82u

#define BAT_ADC_SAMPLES          16u
#define BAT_SETTLE_US            1800u   /* 4τ 稳定时间：R_th = R22 ∥ R3 = 45.05kΩ，τ = 45.05k × 10nF = 0.45ms，4τ = 1.8ms */

static uint8_t g_inited = 0;


#define BAT_ENABLE()    GPIOA_ResetBits(IO_BAT_EN)   /* PA11 拉低 → 导通 */
#define BAT_DISABLE()   GPIOA_SetBits(IO_BAT_EN)     /* PA11 拉高 → 关断 */

static uint16_t adc_sample_mv(void)
{
    uint32_t sum = 0;
    signed short rough = 0;
    uint8_t i;
    int32_t vin_mv;

    BAT_ENABLE();                              /* PA11 拉低，PMOS 导通 */
    GPIOA_ModeCfg(IO_BAT, GPIO_ModeIN_Floating);

    /* 0dB：分压后 1.49~1.89V 正落在手册 0~2V 高精度区 */
    ADC_ExtSingleChSampInit(SampleFreq_3_2, ADC_PGA_0);
    ADC_ChannelCfg(CH_BAT);

    rough = ADC_DataCalib_Rough();
    ADC_ChannelCfg(CH_BAT);

    DelayUs(BAT_SETTLE_US);                    /* 等 4τ，分压网络稳定 */

    for(i = 0; i < BAT_ADC_SAMPLES; i++)
    {
        uint32_t v = ADC_ExcutSingleConver();
        if((int32_t)v + rough > 0)
            v = (uint32_t)((int32_t)v + rough);
        sum += v;
    }

    BAT_DISABLE();

    sum /= BAT_ADC_SAMPLES;

    /* 0dB 公式 Vi = (ADC/2048) * Vref，Vref = 1.05V */
    vin_mv = (int32_t)sum * 1050 / 2048;
    if(vin_mv < 0) vin_mv = 0;

    return (uint16_t)vin_mv;
}

void BSP_BAT_Init(void)
{
    GPIOA_SetBits(IO_BAT_EN);
    GPIOA_ModeCfg(IO_BAT_EN, GPIO_ModeOut_PP_5mA);
    BAT_DISABLE();

    g_inited = 1;
}

uint16_t BSP_BAT_GetMillivolt(void)
{
    uint32_t vbat_mv;

    if(!g_inited)
        BSP_BAT_Init();

    /* 分压反推：VBAT = V_ADC × (R1+R2) / R2 */
    vbat_mv = (uint32_t)adc_sample_mv() * (BAT_DIV_R1_K + BAT_DIV_R2_K) / BAT_DIV_R2_K;

    if(vbat_mv > 4500u) vbat_mv = 4500u;
    if(vbat_mv < 2500u) vbat_mv = 2500u;
    return (uint16_t)vbat_mv;
}

uint8_t BSP_BAT_GetPercent(void)
{
    uint16_t mv = BSP_BAT_GetMillivolt();

    if(mv >= 4200u) return 100u;
    if(mv <= 3300u) return 0u;
    return (uint8_t)(((uint32_t)(mv - 3300u) * 100u) / 900u);
}

uint8_t BSP_BAT_GetLevel(void)
{
    return BSP_BAT_GetPercent();
}