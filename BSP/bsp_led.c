#include "bsp_led.h"
#include "CH59x_pwm.h"
#include "CH59x_gpio.h"

static BSP_LightState_t g_light;
static uint32_t g_tick;

static void pwm_out_channel(uint8_t ch, uint8_t value)
{
    /* PWM cycle is 256; value 0..255 maps to 0..~100% */
    PWMX_ACTOUT(ch, value, High_Level, ENABLE);
}

static void set_all_pwm(uint8_t r, uint8_t g, uint8_t b)
{
    pwm_out_channel(PWM_RED,  r);
    pwm_out_channel(PWM_GREEN,g);
    pwm_out_channel(PWM_BLUE, b);
}

static uint8_t scale_channel(uint8_t color, uint8_t brightness, uint8_t power)
{
    uint32_t v;
    if(!power || brightness == 0 || color == 0)
        return 0;
    v = (uint32_t)color * brightness * (BSP_LIGHT_PWM_CYCLE - 1);
    v /= (BSP_LIGHT_BRIGHTNESS_MAX * BSP_LIGHT_COLOR_MAX);
    return (uint8_t)v;
}

static void apply_fixed(const BSP_LightState_t *p)
{
    uint8_t on = p->power ? 1u : 0u;
    set_all_pwm(scale_channel(p->rgb[0], p->brightness, on),
                scale_channel(p->rgb[1], p->brightness, on),
                scale_channel(p->rgb[2], p->brightness, on));
}

static void rgb_wheel(uint8_t pos, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if(pos < 85)
    {
        *r = 255u - pos * 3u;
        *g = pos * 3u;
        *b = 0u;
    }
    else if(pos < 170)
    {
        pos -= 85u;
        *r = 0u;
        *g = 255u - pos * 3u;
        *b = pos * 3u;
    }
    else
    {
        pos -= 170u;
        *r = pos * 3u;
        *g = 0u;
        *b = 255u - pos * 3u;
    }
}

void BSP_LED_UpdateOutput(void)
{
    BSP_LightState_t tmp = g_light;
    uint8_t speed;

    if(tmp.mode == BSP_LIGHT_MODE_COLORFUL || tmp.mode == BSP_LIGHT_MODE_GRADIENT)
    {
        uint8_t r, g, b;
        uint32_t pos;
        speed = tmp.effectSpeed + 1u;
        pos = (g_tick * speed) >> 2;
        rgb_wheel((uint8_t)(pos & 0xFFu), &r, &g, &b);
        tmp.rgb[0] = r;
        tmp.rgb[1] = g;
        tmp.rgb[2] = b;
    }
    else if(tmp.mode == BSP_LIGHT_MODE_BREATHING)
    {
        uint32_t phase = (g_tick * (tmp.effectSpeed + 1u)) >> 3;
        uint8_t breathe;

        /* 修复：将相位限制在 0~199，避免无符号下溢 */
        phase %= 200;

        if (phase < 100u) {
            breathe = (uint8_t)phase;
        } else {
            breathe = (uint8_t)(200u - phase);
        }

        tmp.brightness = (uint8_t)(((uint32_t)tmp.brightness * breathe) / 100u);
    }

    if(tmp.mode == BSP_LIGHT_MODE_FIXED || tmp.mode == BSP_LIGHT_MODE_BREATHING)
    {
        apply_fixed(&tmp);
    }
    else
    {
        tmp.power = g_light.power;
        set_all_pwm(scale_channel(tmp.rgb[0], tmp.brightness, tmp.power),
                    scale_channel(tmp.rgb[1], tmp.brightness, tmp.power),
                    scale_channel(tmp.rgb[2], tmp.brightness, tmp.power));
    }
}

void BSP_LED_TaskTick(void)
{
    g_tick++;
    if(g_light.power && g_light.mode != BSP_LIGHT_MODE_FIXED)
        BSP_LED_UpdateOutput();
}

void BSP_LED_Init(void)
{
    GPIOA_ResetBits(IO_RED | IO_GREEN);
    GPIOB_ResetBits(IO_BLUE);
    GPIOA_ModeCfg(IO_RED | IO_GREEN, GPIO_ModeOut_PP_5mA);
    GPIOB_ModeCfg(IO_BLUE,          GPIO_ModeOut_PP_5mA);

    PWMX_CLKCfg(1);                    /* PWM base clock = Fsys / 1 = 60 MHz */
    PWMX_CycleCfg(PWMX_Cycle_256);      /* 256 steps per PWM period */

    g_light.power = 0;
    g_light.brightness = BSP_LIGHT_BRIGHTNESS_LIMIT;   /* 默认 100 */
    g_light.mode = BSP_LIGHT_MODE_FIXED;
    g_light.rgb[0] = 255;
    g_light.rgb[1] = 255;
    g_light.rgb[2] = 255;
    g_light.colorTemp = 4000;
    g_light.effectSpeed = 20;
    g_tick = 0;

    set_all_pwm(0, 0, 0);
}

void BSP_LED_SetState(const BSP_LightState_t *pState)
{
    if(!pState)
        return;
    g_light.power = pState->power ? 1u : 0u;

    if(pState->brightness > BSP_LIGHT_BRIGHTNESS_LIMIT)
        g_light.brightness = BSP_LIGHT_BRIGHTNESS_LIMIT;
    else
        g_light.brightness = pState->brightness;

    g_light.mode = pState->mode & 0x03u;
    g_light.rgb[0] = pState->rgb[0];
    g_light.rgb[1] = pState->rgb[1];
    g_light.rgb[2] = pState->rgb[2];
    g_light.colorTemp = pState->colorTemp;
    g_light.effectSpeed = pState->effectSpeed > 100u ? 100u : pState->effectSpeed;
    BSP_LED_UpdateOutput();
}

void BSP_LED_GetState(BSP_LightState_t *pOut)
{
    if(pOut)
        *pOut = g_light;
}

bool BSP_LED_IsPowerOn(void)
{
    return g_light.power ? true : false;
}

void BSP_LED_SetPower(uint8_t on)
{
    g_light.power = on ? 1u : 0u;
    BSP_LED_UpdateOutput();
}