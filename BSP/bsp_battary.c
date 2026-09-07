#include "CH59x_common.h"
#include "bsp_battary.h"

/*
 * The ADC input after the divider is about Vbat * 82/(82+100).
 * Use PGA 0 dB and external single-ended sampling.
 * CH592 ADC is 12-bit, full scale is 0..2.0 V in this configuration.
 */
#define BAT_ADC_FULL_SCALE_MV    2000u
#define BAT_ADC_MAX_CODE         4095u
#define BAT_DIV_R1_K             100u
#define BAT_DIV_R2_K             82u
#define BAT_ADC_SAMPLES          16u

static uint8_t g_inited = 0;

static uint16_t adc_sample_mv(void)
{
    uint32_t sum = 0;
    signed short rough = 0;
    uint8_t i;

    GPIOB_SetBits(IO_BAT_EN);                 /* enable divider path */
    GPIOA_ModeCfg(IO_BAT, GPIO_ModeIN_Floating);

    ADC_ExtSingleChSampInit(SampleFreq_3_2, ADC_PGA_0);
    ADC_ChannelCfg(CH_BAT);

    /* Rough calibration should be done once, but doing it here keeps the code simple. */
    rough = ADC_DataCalib_Rough();
    ADC_ChannelCfg(CH_BAT);

    for(i = 0; i < BAT_ADC_SAMPLES; i++)
    {
        uint32_t v = ADC_ExcutSingleConver();
        if((int32_t)v + rough > 0)
            v = (uint32_t)((int32_t)v + rough);
        sum += v;
    }
    GPIOB_ResetBits(IO_BAT_EN);               /* disable divider to save power */

    sum /= BAT_ADC_SAMPLES;
    return (uint16_t)((sum * BAT_ADC_FULL_SCALE_MV) / BAT_ADC_MAX_CODE);
}

void BSP_BAT_Init(void)
{
    GPIOB_ModeCfg(IO_BAT_EN, GPIO_ModeOut_PP_5mA);
    GPIOB_ResetBits(IO_BAT_EN);
    g_inited = 1;
}

uint16_t BSP_BAT_GetMillivolt(void)
{
    uint32_t vbat_mv;

    if(!g_inited)
        BSP_BAT_Init();

    vbat_mv = (uint32_t)adc_sample_mv() * (BAT_DIV_R1_K + BAT_DIV_R2_K) / BAT_DIV_R2_K;
    if(vbat_mv > 4500u)
        vbat_mv = 4500u;
    if(vbat_mv < 2500u)
        vbat_mv = 2500u;
    return (uint16_t)vbat_mv;
}

uint8_t BSP_BAT_GetPercent(void)
{
    uint16_t mv = BSP_BAT_GetMillivolt();

    if(mv >= 4200u)
        return 100u;
    if(mv <= 3300u)
        return 0u;
    return (uint8_t)(((uint32_t)(mv - 3300u) * 100u) / 900u);
}

uint8_t BSP_BAT_GetLevel(void)
{
    return BSP_BAT_GetPercent();
}
