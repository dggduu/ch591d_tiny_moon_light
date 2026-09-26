#include "CH59x_common.h"
#include "bsp_battary.h"

/* 分压比：BAT_ADC = VBAT × 82/(100+82) ≈ VBAT × 0.4505 */
#define BAT_DIV_R1_K             100u
#define BAT_DIV_R2_K             82u

#define BAT_ADC_SAMPLES          16u
#define BAT_SETTLE_US            3000u   /* 保持 3ms 稳定时间，确保 C10 充满 */

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

    ADC_ExtSingleChSampInit(SampleFreq_3_2, ADC_PGA_1_2);
    ADC_ChannelCfg(CH_BAT);

    rough = ADC_DataCalib_Rough();
    ADC_ChannelCfg(CH_BAT);

    DelayUs(BAT_SETTLE_US);                    /* 等待 RC 网络稳定 */

    for(i = 0; i < BAT_ADC_SAMPLES; i++)
    {
        uint32_t v = ADC_ExcutSingleConver();
        if((int32_t)v + rough > 0)
            v = (uint32_t)((int32_t)v + rough);
        sum += v;
    }

    BAT_DISABLE();

    sum /= BAT_ADC_SAMPLES;

    vin_mv = ((int32_t)sum - 1024) * 1050 / 1024;
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