# dsh-ble-light — CH591D 蓝牙小夜灯控制

把 DSH 的会话状态变成桌上的光：插件用 **Web Bluetooth** 直连 CH591D 小夜灯的
灯控 GATT 服务（`0xFFF0`），运行中呼吸绿、完成常亮粉、等待审批 / 回答跑马灯、空闲
暖白微亮；**空闲时把 0.5–15 分钟写进灯具内部的 RTC 倒计时，到点由灯具自己关灯**
（页面关掉、电脑休眠都不影响）。

| DSH 状态 | 灯光（默认） | 判定依据（宿主实时下发的会话快照） |
| --- | --- | --- |
| 运行中 | 🟢 绿色呼吸 | 任一会话 `running === true` |
| 完成 | 🩷 粉色常亮 | 有会话、非运行、无待处理 |
| 等待审批 | 🟠 琥珀+红跑马灯 | 某会话 `pendingInteraction.kind === 'approval'`（**未知/缺失 kind 也归到这里**） |
| 等待回答 / 计划确认 | 🟣 青+紫跑马灯 | `pendingInteraction.kind === 'question'` 或 `'plan-review'` |
| 空闲（无会话 / 全部空白会话） | 🟡 暖白微亮 + 倒计时自动关灯 | 没有会话，或所有会话 `blank === true` |
| 插件停用 | 关灯 | `enabled: false` |

优先级（自上而下，取最先命中的那个）：

```
待处理（按 kind 细分：approval / questioning） > 运行中 > 空闲（全空白/无会话） > 完成
```

> 平台只会下发 `approval` / `question` / `plan-review` 三种 `kind`，所以不再单列
> 「待处理（类型未知）」兜底态；将来若出现新的 `kind`，一律按 `approval` 处理。
> 状态机参考了 `dsh-whale-pet` 把「审批等待」和「提问等待」分开的做法。

每个状态的颜色、灯效、亮度、速度都可以在设置面板里改，不需要改代码。

## 安装

> 完整步骤（含一键脚本、验证方法、故障排查）见 **[docs/dsh-plugin-install.md](../../docs/dsh-plugin-install.md)**。下面是速查版。

当前是挂在 `web` profile 上，`~/.dsh/profiles/web/package.json`：

```json
"dependencies": {
  "dsh-ble-light": "link:E:/project/wch/ch591d_tiny_moon_light/agent_plugins/dsh_moon_light_ch591d"
}
```

并且 `~/.dsh/profiles/web/node_modules/dsh-ble-light` 必须是指向本目录的**目录链接
（Junction）**，改动才能实时生效。

> **重要**：不要用 GUI 的插件管理或 `dsh plugin add/remove` 去装这个插件。
> pnpm 会把 `link:` 依赖做成**真实副本**，并且 pnpm 在 Windows 上把盘符路径当
> 相对路径解析，结果既拿不到实时更新、链接还是断的（本机踩过两次）。
> 如果需要重装 profile 里的其它插件，装完请检查这个链接并重新接一次：
>
> ```powershell
> $p = "$env:USERPROFILE\.dsh\profiles\web\node_modules\dsh-ble-light"
> Remove-Item -Force -Recurse $p
> New-Item -ItemType Junction -Path $p -Target 'E:\project\wch\ch591d_tiny_moon_light\agent_plugins\dsh_moon_light_ch591d'
> ```
>
> 判断链接是否正常：该目录下 `client.js` 的字节数应与源码一致，且
> `Get-Item $p` 的 `LinkType` 为 `Junction`。改完源码后**刷新页面**即可，
> 不用重启 DSH（客户端模块有 HMR 监听）。

装完**重启 DSH**（客户端插件名录在启动时组合），然后刷新页面 → 打开
**设置 → 蓝牙灯** → 点「连接设备」选择 `CH591D` 即可。

其他环境安装（**Windows 上不建议**，原因见上文 pnpm 警告）：

```sh
dsh plugin --profile <name> add "link:/绝对路径/agent_plugins/dsh_moon_light_ch591d"
```

卸载（Windows 上建议手工改 `package.json` + 删 Junction，见安装指导）：

```sh
dsh plugin --profile <name> remove dsh-ble-light
```

### 浏览器要求

Web Bluetooth 只在**安全上下文**且基于 Chromium 的浏览器里可用：

- 用 Chrome / Edge（Firefox、Safari 不支持 Web Bluetooth）；
- 页面地址必须是 `https://` 或 `http://localhost` / `http://127.0.0.1`
  （DSH Web 默认就是 `127.0.0.1`，属于安全上下文，可以直接用）。

设置面板里会实时显示当前环境是否支持；不支持时会给出提示而不是静默失败。

## 使用

1. 灯具上电（会开始广播，名称 `CH591D`，广播里带 `0xFFF0` 服务）；
2. **设置 → 蓝牙灯 → 连接设备**，在系统弹出的设备选择框里选 `CH591D`；
   选中一次以后浏览器就记住了，之后打开页面会**自动重连**（`getDevices()` 免选择）；
3. 之后 DSH 状态一变，灯就跟着变；页面左下角有一行极小的状态角标（可关闭）。

其他按钮：

- **重连上次**：复用已授权的设备，不需要再弹选择框；
- **断开**：断开 GATT 连接（不会自动重连）；
- **关灯**：立刻关灯并取消灯内倒计时（倒计时取消失败也照样断电）；
- **试灯**：在某个状态卡片上临时套用该灯效，直到状态发生变化为止。

## 设置项

| 分组 | 项 | 说明 |
| --- | --- | --- |
| 连接 | 连接设备 / 重连上次 / 断开 / 关灯 | 设备管理 |
| 总开关 | 启用蓝牙灯 | 关掉即下发关灯并停止驱动 |
| | 打开页面后自动重连 | 用 `getDevices()` 免手势恢复上次设备 |
| | 断线后自动重连 | 指数退避重试，最多 6 次 |
| | 自动开关灯 | 关掉后只改颜色灯效，不动电源 |
| | 显示左下角状态角标 | 连接状态 / 当前状态 / 电量 / 倒计时 |
| | 写入间隔 | 相邻两次 GATT 写入的最小间隔，默认 60 ms |
| 空闲自动关灯 | 空闲时启动倒计时 | 写灯具 `0xFFFA` |
| | 空闲多久后关灯 | **0.5 – 15 分钟**，步进 0.5（换算成固件接受的 30–900 秒） |
| | 「完成」也计入空闲计时 | 默认只对「空闲」计时 |
| 各状态灯效 | 灯效 / 颜色 / 亮度 / 速度 / 换色间隔 | 五个状态各一组 |

灯效取值：

| 值 | 含义 | 颜色是否生效 |
| --- | --- | --- |
| `off` | 关灯 | — |
| `fixed` | 固定色常亮 | ✅ 用颜色列表第 1 个 |
| `breathing` | 呼吸 | ✅ 用颜色列表第 1 个 |
| `gradient` | 彩虹渐变（固件 `mode=1`） | ❌ 固件用彩虹轮覆盖 |
| `colorful` | 跑马灯（固件 `mode=3` COLORFUL） | ❌ 固件用彩虹轮覆盖 |
| `cycle` | **多色跑马灯（软件轮播）** | ✅ 按颜色列表逐个换色 |

> `gradient` 与 `colorful` 在当前固件里走的是同一段彩虹轮代码
> （见 `BSP/bsp_led.c` 的 `BSP_LED_UpdateOutput`），两者只是协议上的 `mode`
> 不同。想让灯按**自己配的颜色**跑马灯，请用 `cycle`：插件会把灯具切到固定色
> 模式，然后按 `换色间隔` 反复写 `0xFFF4`。

颜色最多 6 个，用卡片里的 `+` / `−` 增删。

## 空闲自动关灯怎么实现

0xFFFA 是固件专门为这个插件留的接口：

- 写入 2 字节小端秒数，`30..900` 生效，`0` 表示取消；
- 灯具用自己的 1 秒 tick（`BSP_Ble_Tick`）递减，减到 0 就把 `power` 置 0；
- **计时在灯里，不在浏览器里** —— 关掉页面、电脑睡眠都不影响；
- 离开空闲状态（比如你回来说了句话）插件会立刻写 `0` 取消，工作时不会被误关。

插件在倒计时期间每 5 秒读一次 `0xFFFA`，把灯具实际剩余秒数显示在状态角标和
设置页上。

## 蓝牙协议（与 `BSP/Inc/bsp_ble.h` 一致）

服务 `0xFFF0`，全部是 16 位短 UUID；多字节特征一律**小端**。

| UUID | 名称 | 长度 | 读写 | 说明 |
| --- | --- | --- | --- | --- |
| `0xFFF1` | power | 1 | R/W | `0` 关 / `1` 开 |
| `0xFFF2` | brightness | 1 | R/W | `0..100` |
| `0xFFF3` | mode | 1 | R/W | `0` 固定 / `1` 渐变轮 / `2` 呼吸 / `3` 跑马灯 |
| `0xFFF4` | color | 3 | R/W | R,G,B |
| `0xFFF5` | colorTemp | 2 | R/W | LE，2700..6500（本插件不写） |
| `0xFFF6` | effectSpeed | 1 | R/W | `0..100` |
| `0xFFF7` | timer | 9 | R/W | 墙上时钟定时器（本插件不用，留给灯自带定时） |
| `0xFFF8` | battery | 1 | R/N | 电量百分比，支持通知 |
| `0xFFF9` | rtcTime | 7 | R/W | LE 年 + 月日时分秒（本插件不写） |
| `0xFFFA` | countdown | 2 | R/W | LE 秒；`0` 取消，`30..900` 倒计时关灯 |

写入策略：**只写变化过的特征**，顺序固定为
`颜色 → 亮度 → 速度 → 模式 → 电源`（电源放最后，避免中间态闪一下）；
所有 GATT 操作串行化在一个队列里，避免 `GATT operation already in progress`。
重连后因为灯具状态未知，会全量重发一次。

## 原理（供二次开发）

- 插件是**手写 bundle**，遵循 DSH client-modules 协议：
  `window.__ModuleLoader__.load({ id, factory })`，只依赖平台种子模块 `react`，
  通过 `slots` 服务协作，没有构建步骤。
- 服务端入口 `index.js` 是空的 `apply`：Web Bluetooth 只存在于浏览器，服务端
  不该碰设备；客户端从 `cordis.patch.yml` 的同一行配置里读部署默认值。
- 渲染位置：`shell.overlay`（frame 级浮层，`pointer-events: none`，只放一个
  状态角标）+ `settings.section`（设置页），注册 id 均为 `ble-light`。
- 状态分析：`shell.overlay` 的标准 props 里取两份快照 —— `useSessions`
  （`SessionListState = { ids, byId, phase, ... }`）和 `useSessionStatus`
  （`Map<SessionId, { running, pendingInteraction }>`），由 `deriveMode` 汇总所有
  会话后映射到 approval / questioning / running / idle / success。root 作用域拿不到「当前会话」
  的选择器（`usePanelInfo` 只有 `activePanelId`），所以按优先级汇总而不是挑一个；
  `pendingInteraction` 只在 `useSessionStatus` 里，不在 `SessionSummary` 上。
  `resolveMode` 保留为单会话的纯函数单元。
- 纯逻辑（`deriveMode` / `resolveMode` / `targetFromState` / `planWrites` / `buildDesired` /
  `countdownSeconds`）与蓝牙链路（`createLink`）分离，链路接受注入的
  `bluetooth` 对象，因此可以在 `vm` 沙箱里用 mock GATT 做端到端测试。
- 卸载时通过 `ctx.effect` 断开连接、清掉倒计时轮询与跑马灯定时器。

## 配置优先级

`localStorage`（设置面板里改的）> 插件行 `config`（`cordis.patch.yml` / profile
补丁）> 内置默认。第三方插件的命名空间不在宿主 settings wire 的暴露白名单内，
所以用户配置保存在 localStorage 的 `dsh-ble-light:config`（与 dsh-skin 同一模式）。

**改了 `cordis.patch.yml` 里的默认值后，要在设置面板点一次「恢复默认」才会
生效**（否则 localStorage 里的旧配置优先）。

通过 profile 补丁覆盖部署级默认（不修改插件本体）：

```yaml
# ~/.dsh/profiles/web/cordis.patch.yml
- id: ble-light
  config:
    idleAutoOffMinutes: 10
    states:
      running:
        mode: 'colorful'
        brightness: 80
      approval:
        mode: 'cycle'
        colors: ['#ff0000', '#ffff00']
        intervalMs: 300
      idle:
        mode: 'fixed'
        colors: ['#ffb347']
        brightness: 5
```

## 开发

```sh
node --check client.js      # bundle 语法检查
node scripts/test.cjs       # 逻辑 + 蓝牙链路（mock GATT）测试
```

测试覆盖：bundle 协议与槽位注册、状态映射（含 `kind` 细分、未知 kind 兜底 approval）、
协议换算（含 0.5–15 分钟 → 30–900 秒）、配置合并与夹取（含旧配置里的
`warning` / `unread` 被静默丢弃）、差量写入计划、
mock GATT 下的连接 / 下发 / 倒计时写入与取消 / 软件跑马灯轮播、
**旧固件（缺 0xFFFA / 倒计时写入被拒）下灯效与「关灯」仍必须生效**、
设置页渲染与交互、localStorage 持久化与配置优先级。

验证插件能否被 profile 组合（不会启动服务）：

```sh
dsh --profile <name> --dump-config | grep -A20 'dsh-ble-light'
```

## 文件

| 文件 | 作用 |
| --- | --- |
| `index.js` | 服务端入口（空 apply，满足 loader 要求） |
| `client.js` | 浏览器端：蓝牙链路 + 状态映射 + 设置页（核心实现） |
| `cordis.patch.yml` | 插件行 + 部署级默认配置 |
| `scripts/test.cjs` | 逻辑与链路测试（零依赖，vm 沙箱 + mock GATT） |
| `package.json` | `dsh.bundle` / `dsh.client` 声明 |

## 已知限制

- 一个页面只能连一个灯具；多开标签页会互相抢 GATT 连接。
- 灯具同时只接受一个 BLE 连接（固件在第二路连接时会主动断开）。
- `gradient` / `colorful` 由固件决定颜色，插件配的颜色对它们无效（见上文）。
- 电量通知由固件每 5 分钟推一次，连接时会先读一次。
- **固件必须包含 `0xFFFA`**，否则「空闲自动关灯」不可用。0xFFFA 是后加的特征，
  如果灯上烧的还是旧固件，插件会在设置页提示一次
  「固件没有 0xFFFA 倒计时特征」，点「清除提示」可以关掉。
  注意：倒计时是**可选**能力，它写入失败不会影响灯效和「关灯」按钮
  （早期版本把两者串在一条链上，旧固件下会导致「关灯」完全没反应，已修）。
