# DSH 蓝牙灯插件（dsh-ble-light）安装指导

把 DSH 的会话状态变成桌上的灯光。插件用浏览器的 **Web Bluetooth** 直连 CH591D 小夜灯的灯控 GATT 服务（`0xFFF0`），不经过网关、不需要手机 App。

> 插件本体在 `agent_plugins/dsh_moon_light_ch591d/`，包名 `dsh-ble-light`。
> 本文只讲**怎么装**；插件的状态映射、配置项、协议细节见[插件自带 README](../agent_plugins/dsh_moon_light_ch591d/README.md)。

---

## 一、前置条件

| 项 | 要求 |
| --- | --- |
| DSH | 已安装并至少启动过一次（`~/.dsh/` 目录已生成） |
| Profile | 默认用 `web`（本文以 `web` 为例；换成你的 profile 名即可） |
| 浏览器 | **Chrome 或 Edge**（Firefox / Safari 不支持 Web Bluetooth） |
| 灯具 | 已烧好本仓库固件并上电，广播名为 `CH591D` |
| 系统 | Windows / macOS / Linux 均可；本文命令给的是 **Windows PowerShell** 版本 |

先确认 DSH 可用：

```powershell
dsh --version
Test-Path "$env:USERPROFILE\.dsh\profiles\web"
```

---

## 二、⚠️ 为什么不能用 `dsh plugin add` 或 GUI 插件管理

这个插件是用 **`link:` 本地链接**方式挂载的（改源码立即生效，不用重新安装）。而：

- `dsh plugin add` / GUI 插件管理底层走 **pnpm**，pnpm 会把 `link:` 依赖**拷成真实副本**，之后改源码不再生效；
- pnpm 在 Windows 上还会把 `link:E:/...` 的**盘符当成相对路径**解析，生成一个断链（实测踩过两次）。

所以本插件必须**手工接一个 Junction**。下面每一步都请照做。

> 如果需要用 `dsh plugin add` 装**其它**插件，装完之后请回来检查本文第五节的链接是否还在，断了就重建。

---

## 三、安装步骤

### 1. 找到插件目录

设两个变量（按你自己的路径改）：

```powershell
$PLUGIN     = 'E:\project\wch\ch591d_tiny_moon_light\agent_plugins\dsh_moon_light_ch591d'
$PROFILE_DIR = "$env:USERPROFILE\.dsh\profiles\web"

Test-Path "$PLUGIN\package.json"    # 应为 True
Test-Path "$PLUGIN\client.js"       # 应为 True
```

### 2. 把插件写进 profile 的 `package.json`

编辑 `$PROFILE_DIR\package.json`，加两处：

```jsonc
{
  "dependencies": {
    // ... 其它依赖
    "dsh-ble-light": "link:E:/project/wch/ch591d_tiny_moon_light/agent_plugins/dsh_moon_light_ch591d"
  },
  "dsh": {
    "profile": {
      "bundles": [
        "@deepseek-ai/dsh-base",
        "@deepseek-ai/dsh-web-app",
        // ... 其它 bundle
        "dsh-ble-light"          // ← 必须加进 bundles，否则插件不会被加载
      ]
    }
  }
}
```

> `dependencies` 里的路径用**正斜杠** `/`；`bundles` 里写**包名** `dsh-ble-light`。

### 3. 建目录链接（Junction）

```powershell
$link = "$PROFILE_DIR\node_modules\dsh-ble-light"

# 已有旧链接/副本先删掉
if (Test-Path $link) { [System.IO.Directory]::Delete($link, $false) }

New-Item -ItemType Junction -Path $link -Target $PLUGIN
```

> 用 `[System.IO.Directory]::Delete($p, $false)` 而不是 `Remove-Item -Recurse`，是为了避免个别 PowerShell 版本跟随链接把**源码目录**删掉。

### 4. 重启 DSH

客户端插件名录在 **启动时**组合，所以必须完整重启 DSH 进程（不是刷新页面）。

### 5. 连接灯具

1. 灯具上电（开始广播）
2. 浏览器打开 DSH Web → **设置 → 蓝牙灯**
3. 点「连接设备」，在系统弹窗里选 `CH591D`
4. 选中一次后浏览器会记住，之后打开页面会**自动重连**（`getDevices()` 免选择）

---

## 四、一键脚本（可选）

把下面整段贴进 PowerShell，改一下最上面的两个路径即可：

```powershell
$PLUGIN      = 'E:\project\wch\ch591d_tiny_moon_light\agent_plugins\dsh_moon_light_ch591d'
$PROFILE_DIR = "$env:USERPROFILE\.dsh\profiles\web"
$link        = "$PROFILE_DIR\node_modules\dsh-ble-light"

# 1) 重建 Junction
if (Test-Path $link) { [System.IO.Directory]::Delete($link, $false) }
New-Item -ItemType Junction -Path $link -Target $PLUGIN | Out-Null

# 2) 检查 package.json 是否已登记
$pkg = Get-Content "$PROFILE_DIR\package.json" -Raw | ConvertFrom-Json
"dependencies 里有 dsh-ble-light : " + ($null -ne $pkg.dependencies.'dsh-ble-light')
"bundles 里有 dsh-ble-light      : " + ($pkg.dsh.profile.bundles -contains 'dsh-ble-light')

# 3) 检查链接是否有效
$i = Get-Item $link -Force
"LinkType : $($i.LinkType)"
"Target   : $($i.Target)"
"client.js 字节数 : $((Get-Item "$link\client.js").Length)  （源码: $((Get-Item "$PLUGIN\client.js").Length)）"
```

`dependencies` / `bundles` 两行都应为 `True`，`LinkType` 应为 `Junction`，两个 `client.js` 字节数应一致。

---

## 五、验证

### 1. 链接是否正常

```powershell
$p = "$env:USERPROFILE\.dsh\profiles\web\node_modules\dsh-ble-light"
$i = Get-Item $p -Force
$i.LinkType                                        # 应为 Junction
$i.Target                                          # 应指向插件目录
(Get-Item "$p\client.js").Length                   # 应与源码 client.js 字节数一致
```

### 2. 插件是否被 profile 组合进去

```powershell
dsh --profile web --dump-config | Select-String -Context 0,30 'dsh-ble-light'
```

应该能看到 `- id: ble-light` / `name: dsh-ble-light` 以及 `config.states` 下的 5 个状态。这个命令**不会启动服务**，只是打印组合后的配置，可以放心跑。

### 3. 界面上

重启 DSH → 刷新页面 → **设置** 左侧应出现「**蓝牙灯**」分区；页面左下角会出现一行极小的状态角标（可在设置里关掉）。

---

## 六、日常使用

| 操作 | 说明 |
| --- | --- |
| **连接设备** | 弹系统设备选择框，选 `CH591D` |
| **重连上次** | 复用已授权的设备，不再弹框 |
| **断开** | 断开 GATT 连接（不会自动重连） |
| **关灯** | 立刻关灯并取消灯内倒计时 |
| **试灯** | 在某个状态卡片上临时套用该灯效，直到状态变化为止 |
| **恢复默认** | 清掉浏览器里的本地配置，回到 `cordis.patch.yml` 的部署默认值 |

配置优先级：**localStorage（设置页里改的）> 插件行 `config`（`cordis.patch.yml`）> 内置默认**。

> 改了 `cordis.patch.yml` 里的默认值后，要在设置面板点一次「**恢复默认**」才会生效（否则 localStorage 里的旧配置优先）。

---

## 七、更新与卸载

### 更新

因为挂的是 Junction，**改插件源码不需要重装**：

- 改 `client.js` → 直接**刷新页面**即可（客户端模块有 HMR 监听）
- 改 `cordis.patch.yml` → 属于服务端 patch，需要**重启 DSH**

### 卸载

1. 编辑 `$PROFILE_DIR\package.json`：删掉 `dependencies` 里的 `dsh-ble-light`，以及 `bundles` 里的 `dsh-ble-light`
2. 删掉链接：`[System.IO.Directory]::Delete("$PROFILE_DIR\node_modules\dsh-ble-light", $false)`
3. 重启 DSH

> **不要**用 `dsh plugin --profile web remove dsh-ble-light` —— 会走 pnpm，见第二节。

---

## 八、常见问题

| 现象 | 原因 / 处理 |
| --- | --- |
| 设置里**看不到**「蓝牙灯」 | ① `bundles` 数组里漏了 `dsh-ble-light`；② 装完没**重启 DSH**（只刷新页面不够） |
| 能看到设置页，但**点「连接设备」没反应** | 浏览器不是 Chrome / Edge；或页面不是安全上下文（必须 `https://` 或 `http://localhost` / `http://127.0.0.1`）。设置页里会实时显示当前环境是否支持 |
| 改了源码**不生效** | Junction 被破坏了（多半是跑过 `pnpm install` / `dsh plugin add`）。按第五节检查，重建链接 |
| 链接变成**断链**、`node_modules` 里报 `os error 123` | pnpm 把 `link:E:/...` 的盘符当相对路径解析了。删掉断链，按第三节第 3 步重建 |
| 提示**「固件没有 0xFFFA 倒计时特征」** | 灯上烧的是旧固件。「空闲自动关灯」不可用，但**灯效与「关灯」按钮仍正常**；点「清除提示」可关掉该提示 |
| 一个页面连不上第二个灯 | 一个页面只能连一个灯具，多开标签页会互相抢 GATT 连接 |
| 手机 / 别的 App 连不上灯 | 灯具同时只接受一个 BLE 连接，第二路会被固件主动断开；先关掉浏览器这边的连接 |

---

## 九、开发调试

插件是**手写的 client bundle**，遵循 DSH client-modules 协议（`window.__ModuleLoader__.load({ id, factory })`），只依赖平台种子模块 `react`，**没有构建步骤**。

```sh
cd agent_plugins/dsh_moon_light_ch591d

node --check client.js      # bundle 语法检查
node scripts/test.cjs       # 逻辑 + 蓝牙链路（mock GATT）测试
```

纯逻辑（`deriveMode` / `resolveMode` / `targetFromState` / `planWrites` / `buildDesired` / `countdownSeconds`）与蓝牙链路（`createLink`）分离，链路接受注入的 `bluetooth` 对象，所以能在 `vm` 沙箱里用 mock GATT 做端到端测试，不需要真实硬件。

### 渲染位置

- `shell.overlay` — 页面左下角的状态角标（`pointer-events: none`）
- `settings.section` — 设置页分区

注册 id 均为 `ble-light`。
