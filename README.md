# CH591D 蓝牙小夜灯 · Agent 状态指示灯

- 主控：**CH591D**（RISC-V + 内置 BLE，192 KB Flash / 26 KB SRAM）
- 无线：BLE，自定义灯控 GATT 服务 `0xFFF0`
- 供电：1S 锂电池 + USB Type-C 充电
- 上位机：配套 DSH 插件 [`dsh-ble-light`](agent_plugins/dsh_moon_light_ch591d/)，用浏览器的 Web Bluetooth 直连，无需网关 / App

## 开源协议

MIT License。硬件与源码均开源。
