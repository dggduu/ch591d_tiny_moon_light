# CH591D 蓝牙小夜灯 · Agent 状态指示灯

> 立创电赛作品。给一个开口只有 2.6 cm 的 3D 打印月亮灯灯罩，自己做一块能塞进去的彩色 LED 控制板；灯的颜色随 AI Agent（DSH / DeepSeek Harness）的工作状态实时变化，抬头就知道它在忙还是在等你。

- 主控：**CH591D**（RISC-V + 内置 BLE，192 KB Flash / 26 KB SRAM）
- 无线：BLE，自定义灯控 GATT 服务 `0xFFF0`
- 供电：1S 锂电池 + USB Type-C 充电
- 上位机：配套 DSH 插件 [`dsh-ble-light`](agent_plugins/dsh_moon_light_ch591d/)，用浏览器的 Web Bluetooth 直连，无需网关 / App

---

## 功能

| 功能 | 说明 |
| --- | --- |
| **Agent 状态灯** | 运行中 / 完成 / 等待审批 / 等待回答 / 空闲 —— 五态对应五种灯光，由 DSH 插件实时下发 |
| **多种灯效可配** | 固定色 / 呼吸 / 彩虹渐变 / 跑马灯，颜色、亮度、速度都能在插件设置页里改，不用改代码 |
| **电量可见** | 板载 12 位 ADC 采集电池电压，经 BLE 上报电量百分比 |
| **空闲自动关灯** | 空闲 0.5~15 分钟后由**灯具内部倒计时**自己断电，关掉页面、电脑休眠都不影响 |
| **二次开发友好** | 完整 GATT 表（10 条特征），任意 BLE 客户端都能直接控制 |

---

## 硬件

### 电源链路

```
USB Type-C ──┬─► TP4057 充电管理 ──► 1S 锂电池 ──┐
             │                                    ├─► TLV75733 (3.3V LDO) ─► 3V3 ─► MCU + LED
             └─► 1N5819WS + AO3401 电源路径 ──────┘
```

- USB-C 的 CC1 / CC2 各接 5.1 kΩ 下拉（Sink 设备），VBUS 并 4.7 µF 去耦
- TP4057 线性充电，恒流值由 PROG 电阻决定：**I_BAT = 1000 / R_PROG**（R_PROG 单位 kΩ，I_BAT 单位 mA），本设计取 10 kΩ → **100 mA**
- `CHRG#` / `STDBY#` 各驱动一颗指示灯（充电中 / 已充满）
- LED 峰值电流大，所以**没有把 VBAT 直接接到 MCU 供电脚**，统一走 LDO 产生的 3V3，保证灯亮时 MCU 电源不塌陷
- `SK12D07VG5` 拨动开关控制 LDO 的 `PWR_EN`，实现真正的断电关机

### LED 驱动

- 3 颗三色（R/G/B）LED 共 9 路，每路串限流电阻：**红 68 Ω，蓝 / 绿 150 Ω**
- 每个颜色通道由一颗 **AO3400** N 沟道 MOSFET 做低边开关，栅极 100 kΩ 下拉（保证 MCU 复位期间灯不乱闪）
- 三路控制信号接 MCU 的 `PWM4`(PA12) / `PWM5`(PA13) / `PWM7`(PB4)

### 电池电压采集

- 分压：**R22 = 100 kΩ（上臂）/ R3 = 82 kΩ（下臂）**，分压比 82/182 ≈ 0.45，4.2 V 满电时 ADC 端约 **1.89 V**
- 用 **AO3401** P-MOS 做高边开关，只在测量瞬间接通分压网络，避免 100 kΩ 分压电阻长期漏电
- 12 位 ADC，`Vref`(VINTA) ≈ 1.05 V，增益取 **0 dB**（高精度区 0~2 V 正好覆盖工作点），分辨率约 1.14 mV/LSB
- RC 稳定时间：`R_th = R22 ∥ R3 = 45.05 kΩ`，`τ = R_th × C10 = 0.45 ms`，固件等 **4τ ≈ 1.8 ms**

### 主控最小系统

- **CH591D**，QFN20，RISC-V RV32IMAC + 内置 2.4 GHz BLE
- **只用 1 颗 32 MHz 无源晶振**（BLE 射频 + 系统时钟），不做 32.768 kHz 低速晶振 —— 本作品不休眠，时基直接复用芯片内部 32 kHz RC，省一颗料
- **不启用内部 DCDC**（省一颗电感），代价是 BLE 工作电流略大
- 预留 BOOT 按键 + 4 针排针（BOOT / TX / RX / GND），支持串口 ISP 下载；也可用 WCH-Link 两线调试口烧录

### 天线与 PCB

- 板载 2.4 GHz 天线，馈线按 **50 Ω 共面单端阻抗**设计（线宽 7 mil、间距 4 mil），并做天线净空
- 2 层板，板厚 1.6 mm，外层铜厚 1 oz

### 关键器件

| 位号 | 器件 | 型号 / 规格 |
| --- | --- | --- |
| U1 | 主控 MCU | CH591D |
| U6 | 锂电充电管理 | TP4057（PROG = 10 kΩ → 100 mA） |
| U4 | LDO | TLV75733PDBVR（3.3 V） |
| U3 / U5 | P 沟道 MOSFET | AO3401CI-ES |
| Q1 / Q2 / Q4 | N 沟道 MOSFET | AO3400 |
| D1 | 肖特基二极管 | 1N5819WS |
| X2 | 无源晶振 | 32 MHz |
| SW3 / SW4 | 按键 / 拨动开关 | TS-1088-AR02016 / SK12D07VG5 |
| USB2 | USB Type-C 母座 | 16P |
| U2 | 电池连接器 | HC-1.25-2PLT |
| H1 | 排针 | PZ254V-11-04P（2.54 mm 4P） |
| LED2/3/4 | 三色 LED | RGB 三色 6 脚 |
| LED1/6 | 指示灯 | 贴片 LED（充电中 / 已充满） |

---

## 固件

### 源码结构

| 目录 | 内容 |
| --- | --- |
| `User/` | `Main.c`，应用入口 |
| `BSP/` | 板级驱动：`bsp_led.c`（PWM 灯效）、`bsp_ble.c`（GATT 服务）、`bsp_battary.c`（ADC 电量）、`bsp_rtc.c`（软件墙钟）、`bsp_flash.c`（参数掉电保存）、`bsp_uart.c`（调试串口） |
| `HAL/` | 沁恒 HAL：`MCU.c`（BLE 硬件初始化）、`RTC.c`（TMOS 时基）、`SLEEP.c`、`LED.c`、`KEY.c` |
| `StdPeriphDriver/` | 沁恒 CH59x 标准外设库 + 寄存器定义 |
| `component/BLE/` | BLE 协议栈 `libCH59xBLE.a`（V1.4）与头文件 |
| `RVMSIS/` `Startup/` `Ld/` | RISC-V 内核支持、启动汇编、链接脚本 |
| `agent_plugins/` | 配套的 DSH 上位机插件 |

> ⚠️ 本工程的 `.c/.h` 混合了 **GBK**（`BSP/bsp_battary.c`、`BSP/bsp_led.c`、`BSP/Inc/bsp_led.h` 等）与 **UTF-8**（`BSP/bsp_ble.c`）两种编码，行尾统一 CRLF。用编辑器改文件前请先确认编码，否则中文注释会变乱码。

### 编译与烧录

1. 用 **MounRiver Studio** 打开 `CH591D.wvproj`，直接 Build
   - 工具链：RISC-V Embedded GCC8，`rv32imac / ilp32`，`-Os`
   - 链接脚本 `Ld/Link.ld`：**Flash 192 KB @ 0x00000000，RAM 26 KB @ 0x20000000**
   - 产物在 `obj/` 下（`CH591D.elf` / `.hex` / `.map`）
2. 烧录方式一：**WCH-Link** 两线调试口直接下载 / 在线调试
3. 烧录方式二：按住 **BOOT** 键上电进入 ROM BootLoader，经 UART1（PA8 = RX / PA9 = TX，115200 8N1）串口下载

> 调试串口同样是 UART1，115200，上电会打印启动日志。

### 运行架构

系统时钟 `SetSysClock(CLK_SOURCE_PLL_60MHz)` → **Fsys = 60 MHz**。不跑 RTOS，直接用沁恒的 **TMOS 事件调度器**，主循环只有 `TMOS_SystemProcess()`：

| 事件 | 周期 | 作用 |
| --- | --- | --- |
| `BSP_BLE_TICK_EVT` | 50 ms | 灯效动画步进；每满 1 s 推进软件墙钟、递减关灯倒计时 |
| `BSP_BLE_BATTERY_EVT` | 5 min | 通过 `0xFFF8` 通知电量 |
| `BSP_BLE_SAVE_EVT` | 变更后 1 s | 灯效参数写 Flash（防抖，避免频繁擦写） |
| `HAL_REG_INIT_EVENT` | 120 s | BLE 射频温漂校准 |

TMOS 时基取自芯片内部 32 kHz RTC 计数器，因此不需要外挂 32.768 kHz 晶振。

### PWM 与灯效

```c
PWMX_CLKCfg(1);                 /* PWM 时钟 = Fsys / 1 = 60 MHz */
PWMX_CycleCfg(PWMX_Cycle_256);  /* 8 位模式，周期 256 */
PWMX_ACTOUT(ch, value, High_Level, ENABLE);   /* value: 0..255 */
```

- 输出频率 = 60 MHz / 256 = **234.375 kHz**，8 位（256 级）占空比，远高于人眼闪烁感知频率
- 调光两级解耦：`占空比 = 颜色 × 亮度 / 100`
- 四种灯效：固定色 / 彩虹渐变 / 呼吸（三角波调亮度）/ 跑马灯

### 电池电压采集

```c
ADC_ExtSingleChSampInit(SampleFreq_3_2, ADC_PGA_0);  /* 0 dB，采样保持时间最长 */
```

用最低采样时钟（3.2 MHz）换取最长采样窗口，匹配 45 kΩ 的高阻分压源；连采 16 次取平均并叠加粗校准偏移。电量映射 3.30 V → 0%、4.20 V → 100%。

---

## BLE GATT 协议（便于二次开发）

广播名 `CH591D`，主服务 `0xFFF0`，全部为 16 位短 UUID；**多字节特征一律小端**。

| UUID | 名称 | 长度 | 权限 | 说明 |
| --- | --- | --- | --- | --- |
| `0xFFF0` | Light Service | — | — | 灯控主服务 |
| `0xFFF1` | power | 1 | 读 / 写 | 0 关灯，1 开灯 |
| `0xFFF2` | brightness | 1 | 读 / 写 | 0~100 |
| `0xFFF3` | mode | 1 | 读 / 写 | 0 固定 / 1 渐变 / 2 呼吸 / 3 跑马灯 |
| `0xFFF4` | color | 3 | 读 / 写 | R、G、B 各 1 字节 |
| `0xFFF5` | colorTemp | 2 | 读 / 写 | 色温 2700~6500 K（预留） |
| `0xFFF6` | effectSpeed | 1 | 读 / 写 | 灯效速度 0~100 |
| `0xFFF7` | timer | 9 | 读 / 写 | 墙钟定时器（预留） |
| `0xFFF8` | battery | 1 | 读 / 通知 | 电量百分比；连接时推一次，之后每 5 分钟推一次 |
| `0xFFF9` | rtcTime | 7 | 读 / 写 | 年 + 月日时分秒 |
| `0xFFFA` | countdown | 2 | 读 / 写 | **倒计时自动关灯**：小端秒数，0 = 取消，30~900 = N 秒后关灯 |

写入链路：BLE 写请求 → `BSP_LightWriteAttrCB()`（校验长度与范围）→ `BSP_LED_SetState()`（夹取亮度 / 模式 / 速度）→ `PWMX_ACTOUT()` 写占空比。

`0xFFFA` 的倒计时由灯具自己的 1 s tick 递减，**到点由灯自己断电**，不依赖手机或电脑保持在线；读回该特征返回的是实时剩余秒数。

连接参数：广播间隔 100 ms，期望连接间隔 7.5 ms ~ 1.25 s；只允许 1 路连接，第二路会被主动断开。

---

## 上位机：DSH 蓝牙灯插件

插件 `dsh-ble-light` 挂在 DSH 的 `web` profile 上，用浏览器的 **Web Bluetooth** 直连灯控 GATT 服务，把 Agent 的会话状态翻译成灯光。

| DSH 状态 | 灯光（默认） | 判定依据 |
| --- | --- | --- |
| 运行中 | 🟢 绿色呼吸 | 任一会话 `running === true` |
| 完成 | 🩷 粉色常亮 | 有会话、非运行、无待处理 |
| 等待审批 | 🟠 琥珀 + 红跑马灯 | `pendingInteraction.kind === 'approval'` |
| 等待回答 / 计划确认 | 🟣 青 + 紫跑马灯 | `kind === 'question'` 或 `'plan-review'` |
| 空闲 | 🟡 暖白微亮 + 倒计时关灯 | 无会话或全为空白会话 |

**安装指导：[docs/dsh-plugin-install.md](docs/dsh-plugin-install.md)**

插件源码与自身文档：[`agent_plugins/dsh_moon_light_ch591d/`](agent_plugins/dsh_moon_light_ch591d/)

---

## 快速上手

1. 编译固件并烧录到 CH591D（见上文「编译与烧录」）
2. 给灯具上电（会开始广播，名称 `CH591D`）
3. 按 [安装指导](docs/dsh-plugin-install.md) 装好 DSH 插件并重启 DSH
4. 浏览器打开 DSH Web → **设置 → 蓝牙灯 → 连接设备** → 在系统弹窗里选 `CH591D`
5. 之后 DSH 状态一变，灯就跟着变

---

## 已知问题

- **蓝绿 PWM 映射**：原理图标的是 `PWM5 → BLUE_CONTROL`、`PWM7 → GREEN_CONTROL`，固件里是 `PWM_GREEN = CH_PWM5`、`PWM_BLUE = CH_PWM7`，两者互换。以实物为准，如发现蓝绿反色请对照修改 `BSP/Inc/bsp_led.h`。
- **BOOT 按键引脚**：原理图把 BOOT 接在 PB7，而 CH59x ROM BootLoader 的 boot 引脚是 PB22（默认）/ PB11（备用）。串口 ISP 下载若无效，请核对这一点；用 WCH-Link 烧录不受影响。
- 本固件**不开低功耗**（`HAL_SLEEP = FALSE`），主循环是纯事件调度，BLE 链路常驻在线，换取「连上就一直响应」的体验。

## 开源协议

MIT License。硬件与源码均开源。
