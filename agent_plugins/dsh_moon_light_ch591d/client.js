/*
 * dsh-ble-light — CH591D 蓝牙小夜灯控制插件（浏览器端 client bundle）
 *
 * 用 Web Bluetooth 直连 CH591D 的灯控 GATT 服务（0xFFF0），把 DSH 的会话状态
 * 实时映射成灯光：
 *
 *   运行中（agent 正在工作）     -> 绿色呼吸
 *   完成 / 空闲会话              -> 粉色常亮
 *   等待审批（含未知 kind）      -> 琥珀 + 红软件跑马灯（优先级最高）
 *   等待回答 / 计划确认          -> 青 + 紫软件跑马灯
 *   空闲（无会话 / 空白会话）     -> 暖白微亮 + 灯内 RTC 倒计时自动关灯
 *   插件停用                     -> 关灯
 *
 * 「空闲自动关灯」不靠浏览器定时器：插件把 0.5–15 分钟换算成秒写进灯具的
 * 0xFFFA 倒计时特征，灯具用自己的 1s RTC tick 计时，到点自动断电。页面关掉、
 * 电脑休眠都不影响它 —— 这是固件专门为这个插件留的接口。
 *
 * 协议（与固件 BSP/Inc/bsp_ble.h 一一对应，16 位短 UUID，服务 0xFFF0）：
 *   0xFFF1 power       1B   0/1
 *   0xFFF2 brightness  1B   0..100
 *   0xFFF3 mode        1B   0 固定 | 1 渐变轮 | 2 呼吸 | 3 跑马灯(COLORFUL)
 *   0xFFF4 color       3B   R,G,B（fixed/breathing 生效；1/3 模式固件用彩虹轮覆盖）
 *   0xFFF5 colorTemp   2B   LE，2700..6500（本插件不写，留给灯自带色温）
 *   0xFFF6 effectSpeed 1B   0..100
 *   0xFFF7 timer       9B   墙上时钟定时器（本插件不用）
 *   0xFFF8 battery     1B   只读 + 通知
 *   0xFFF9 rtcTime     7B   LE 年 + 月日时分秒（本插件不写）
 *   0xFFFA countdown   2B   LE 秒；0 = 取消，30..900 = N 秒后关灯
 *
 * 配置持久化在 localStorage（第三方命名空间不在宿主 settings wire 的白名单内，
 * 与 dsh-skin / dsh-mood-light 同一模式），同浏览器生效。
 *
 * 手写 bundle 遵循 DSH client-modules 协议：
 *   window.__ModuleLoader__.load({ id, factory })，
 *   factory(require) 返回 module.exports = { inject, apply }。
 */
window.__ModuleLoader__.load({
  id: "dsh-ble-light",
  factory: (require) => {
    "use strict";
    var module = { exports: {} };
    var exports = module.exports;
    var React = require("react");
    var h = React.createElement;

    /* ============================================================
     *  一、CH591D 灯控 GATT 协议
     * ============================================================ */
    var SERVICE_UUID = 0xfff0;
    var CH = {
      power: 0xfff1,
      brightness: 0xfff2,
      mode: 0xfff3,
      color: 0xfff4,
      colorTemp: 0xfff5,
      effectSpeed: 0xfff6,
      timer: 0xfff7,
      battery: 0xfff8,
      rtcTime: 0xfff9,
      countdown: 0xfffa
    };
    /* 0xFFF7 timer 的 packed 结构体长度（固件 BSP_RtcTimer_t；本插件不写该特征） */
    var TIMER_LEN = 10;
    /* 固件只接受 30..900 秒的倒计时（见 BSP_BLE_COUNTDOWN_MIN_SEC / _MAX_SEC） */
    var COUNTDOWN_MIN_SEC = 30;
    var COUNTDOWN_MAX_SEC = 900;
    var DEFAULT_DEVICE_NAME = "CH591D";
    /* DS 状态 -> 固件 mode 值；'cycle' 是软件轮播，底层停在固定色模式 */
    var MODE_CODE = { fixed: 0, gradient: 1, breathing: 2, colorful: 3, cycle: 0 };
    var EFFECT_OPTIONS = [
      { value: "off", label: "关闭（关灯）" },
      { value: "fixed", label: "固定色" },
      { value: "breathing", label: "呼吸" },
      { value: "gradient", label: "彩虹渐变（硬件）" },
      { value: "colorful", label: "跑马灯（硬件 COLORFUL）" },
      { value: "cycle", label: "多色跑马灯（软件轮播）" }
    ];
    var EFFECT_VALUES = EFFECT_OPTIONS.map(function (o) { return o.value; });

    /*
     * 状态集合。approval / questioning 是按 pendingInteraction.kind 拆出来的细分态
     * （参考 dsh-whale-pet 把「审批等待」和「提问等待」分开的做法）；
     * 认不出的 kind 一律归入 approval，不再单列兜底态。
     */
    var STATE_KEYS = ["running", "success", "approval", "questioning", "idle"];
    var STATE_LABELS = {
      running: "运行中",
      success: "完成",
      approval: "等待审批",
      questioning: "等待回答",
      idle: "空闲"
    };
    var STATE_HINTS = {
      running: "agent 正在工作",
      success: "一轮对话已结束",
      approval: "有工具调用等你批准",
      questioning: "有问题 / 计划确认等你回答",
      idle: "无会话或空白会话，可启动倒计时自动关灯"
    };

    /*
     * pendingInteraction.kind -> 细分状态。
     * 平台侧的 kind 目前由两个插件声明：dsh-client-ui-approval 用 'approval'，
     * dsh-client-ui-user-questions 用 'question'（问题与计划确认共用）。
     * 没见过的 kind 落到 approval 兜底（语义最接近：未知交互绝大多数是审批）。
     */
    function stateFromInteractionKind(kind) {
      var k = String(kind === undefined || kind === null ? "" : kind).toLowerCase();
      if (k === "approval") return "approval";
      if (k === "question" || k === "plan-review" || k === "questioning") return "questioning";
      return "approval";
    }

    var MAX_COLORS = 6;
    var MIN_MARQUEE_MS = 150;
    var MAX_MARQUEE_MS = 5000;

    /* ============================================================
     *  二、通用工具
     * ============================================================ */
    function isNum(v) {
      return typeof v === "number" && isFinite(v);
    }
    function num(v, dflt) {
      return isNum(v) ? v : dflt;
    }
    function clampNum(v, lo, hi, dflt) {
      var n = num(v, dflt === undefined ? lo : dflt);
      return Math.max(lo, Math.min(hi, n));
    }
    function clampInt(v, lo, hi, dflt) {
      return Math.round(clampNum(v, lo, hi, dflt));
    }
    function pad2(n) {
      return (n < 10 ? "0" : "") + n;
    }
    /** 秒数 -> "m:ss"，用于角标与设置页的倒计时显示。 */
    function formatRemain(sec) {
      var s = Math.max(0, Math.round(num(sec, 0)));
      return Math.floor(s / 60) + ":" + pad2(s % 60);
    }
    /** 2 字节小端（固件里所有多字节特征都是 LE）。 */
    function uint16le(v) {
      var n = Math.max(0, Math.min(65535, Math.round(num(v, 0))));
      return [n & 0xff, (n >> 8) & 0xff];
    }
    function sameBytes(a, b) {
      if (!a || !b || a.length !== b.length) return false;
      for (var i = 0; i < a.length; i++) {
        if (a[i] !== b[i]) return false;
      }
      return true;
    }
    /** "#abc" / "#aabbcc" -> "#aabbcc"；非法返回 null。 */
    function normalizeHex(value) {
      var m = /^#?([0-9a-f]{3}|[0-9a-f]{6})$/i.exec(String(value === undefined ? "" : value).trim());
      if (!m) return null;
      var body = m[1].toLowerCase();
      if (body.length === 3) {
        body = body[0] + body[0] + body[1] + body[1] + body[2] + body[2];
      }
      return "#" + body;
    }
    /** "#rrggbb" -> [r,g,b]；非法返回白色。 */
    function hexToRgb(value) {
      var hex = normalizeHex(value) || "#ffffff";
      return [
        parseInt(hex.slice(1, 3), 16),
        parseInt(hex.slice(3, 5), 16),
        parseInt(hex.slice(5, 7), 16)
      ];
    }
    /** 0.5–15 分钟 -> 固件要求的 30..900 秒（越界自动夹取）。 */
    function countdownSeconds(minutes) {
      var sec = Math.round(clampNum(minutes, 0.5, 15, 5) * 60);
      return Math.max(COUNTDOWN_MIN_SEC, Math.min(COUNTDOWN_MAX_SEC, sec));
    }
    /** 长 UUID（0000fff1-0000-...）-> 16 位数值；非短 UUID 返回 null。 */
    function shortUuid(uuid) {
      var m = /^0000([0-9a-f]{4})-0000-1000-8000-00805f9b34fb$/i.exec(String(uuid));
      return m ? parseInt(m[1], 16) : null;
    }
    function hex4(code) {
      var s = code.toString(16);
      while (s.length < 4) s = "0" + s;
      return "0x" + s.toUpperCase();
    }
    /** Web Bluetooth 的原生错误信息大多是英文，这里做一层人话翻译。 */
    function describeError(err) {
      var raw = err && err.message ? String(err.message) : String(err || "");
      if (/cancelled|canceled|chooser/i.test(raw)) return "已取消设备选择";
      if (/adapter not available|Bluetooth adapter/i.test(raw)) return "蓝牙适配器不可用（请在系统里打开蓝牙）";
      if (/permission|denied|not allowed/i.test(raw)) return "没有蓝牙权限，请在弹出的授权框里允许";
      if (/GATT operation already in progress/i.test(raw)) return "上一个蓝牙操作还没完成，请稍后重试";
      if (/not found|no device|NotFoundError/i.test(raw)) return "没找到设备：请确认灯已上电并在广播范围内";
      if (/NetworkError|Connection failed|disconnected/i.test(raw)) return "蓝牙连接已断开";
      if (/SecurityError/i.test(raw)) return "需要 HTTPS 或 localhost 才能使用 Web Bluetooth";
      return raw || "蓝牙操作失败";
    }

    /* ============================================================
     *  三、默认配置 + 合并
     * ============================================================ */
    var DEFAULT_CONFIG = {
      enabled: true,                    /* 总开关 */
      autoConnect: true,                /* 打开页面后自动重连上次授权的设备 */
      autoPower: true,                  /* 状态驱动时自动开灯；false 时不动电源 */
      reconnect: true,                  /* 意外断线后自动重连 */
      showStatus: true,                 /* 页面左下角状态角标 */
      writeGapMs: 60,                   /* 相邻两次特征写入的最小间隔 */
      idleAutoOffEnabled: true,         /* 空闲自动关灯（用灯内 RTC 倒计时） */
      idleAutoOffMinutes: 5,            /* 0.5 – 15 分钟 */
      idleAutoOffIncludeSuccess: false, /* 「完成」是否也计入空闲计时 */
      states: {
        running: { mode: "breathing", colors: ["#10b981"], brightness: 60, speed: 40, intervalMs: 600 },
        success: { mode: "fixed", colors: ["#ec4899"], brightness: 40, speed: 30, intervalMs: 600 },
        /* 等待审批（也是未知 kind 的兜底）：琥珀 + 红，提醒你去点批准 */
        approval: { mode: "cycle", colors: ["#f59e0b", "#ef4444"], brightness: 72, speed: 45, intervalMs: 420 },
        /* 等待回答 / 计划确认：青 + 紫，和审批区分开 */
        questioning: { mode: "cycle", colors: ["#22d3ee", "#a855f7"], brightness: 68, speed: 40, intervalMs: 500 },
        idle: { mode: "fixed", colors: ["#ffd9a0"], brightness: 8, speed: 20, intervalMs: 800 }
      }
    };

    function mergeStateConfig(def, user) {
      var u = user && typeof user === "object" ? user : {};
      var mode = EFFECT_VALUES.indexOf(u.mode) >= 0 ? u.mode : def.mode;
      var colors = [];
      if (Array.isArray(u.colors)) {
        for (var i = 0; i < u.colors.length && colors.length < MAX_COLORS; i++) {
          var hex = normalizeHex(u.colors[i]);
          if (hex) colors.push(hex);
        }
      }
      if (!colors.length) colors = def.colors.slice();
      return {
        mode: mode,
        colors: colors,
        brightness: clampInt(u.brightness, 0, 100, def.brightness),
        speed: clampInt(u.speed, 0, 100, def.speed),
        intervalMs: clampInt(u.intervalMs, MIN_MARQUEE_MS, MAX_MARQUEE_MS, def.intervalMs)
      };
    }

    /** 深度合并配置：只覆盖给出的字段，states 逐状态合并，数值一律夹取到合法区间。 */
    function mergeConfig(cfg) {
      var c = cfg && typeof cfg === "object" ? cfg : {};
      var merged = {
        enabled: typeof c.enabled === "boolean" ? c.enabled : DEFAULT_CONFIG.enabled,
        autoConnect: typeof c.autoConnect === "boolean" ? c.autoConnect : DEFAULT_CONFIG.autoConnect,
        autoPower: typeof c.autoPower === "boolean" ? c.autoPower : DEFAULT_CONFIG.autoPower,
        reconnect: typeof c.reconnect === "boolean" ? c.reconnect : DEFAULT_CONFIG.reconnect,
        showStatus: typeof c.showStatus === "boolean" ? c.showStatus : DEFAULT_CONFIG.showStatus,
        writeGapMs: clampInt(c.writeGapMs, 0, 1000, DEFAULT_CONFIG.writeGapMs),
        idleAutoOffEnabled:
          typeof c.idleAutoOffEnabled === "boolean" ? c.idleAutoOffEnabled : DEFAULT_CONFIG.idleAutoOffEnabled,
        idleAutoOffMinutes: clampNum(c.idleAutoOffMinutes, 0.5, 15, DEFAULT_CONFIG.idleAutoOffMinutes),
        idleAutoOffIncludeSuccess:
          typeof c.idleAutoOffIncludeSuccess === "boolean"
            ? c.idleAutoOffIncludeSuccess
            : DEFAULT_CONFIG.idleAutoOffIncludeSuccess,
        states: {}
      };
      /* 分钟只保留到 0.5 的粒度，避免界面出现 5.0000001 这种值 */
      merged.idleAutoOffMinutes = Math.round(merged.idleAutoOffMinutes * 2) / 2;
      for (var i = 0; i < STATE_KEYS.length; i++) {
        var key = STATE_KEYS[i];
        merged.states[key] = mergeStateConfig(
          DEFAULT_CONFIG.states[key],
          c.states ? c.states[key] : undefined
        );
      }
      return merged;
    }

    /* ============================================================
     *  四、配置存储（localStorage + 内存订阅）
     * ============================================================ */
    var STORAGE_KEY = "dsh-ble-light:config";
    var DEVICE_KEY = "dsh-ble-light:device";
    var STYLE_ID = "dsh-ble-light-style";

    var configListeners = [];
    var configState = null;

    function storageGet(key) {
      if (typeof localStorage === "undefined") return null;
      try {
        return localStorage.getItem(key);
      } catch (e) {
        return null;
      }
    }
    function storageSet(key, value) {
      if (typeof localStorage === "undefined") return;
      try {
        localStorage.setItem(key, value);
      } catch (e) {
        /* 忽略配额 / 隐私模式错误 */
      }
    }
    function readStoredConfig() {
      var raw = storageGet(STORAGE_KEY);
      if (!raw) return null;
      try {
        return JSON.parse(raw);
      } catch (e) {
        return null;
      }
    }
    function notifyConfig() {
      for (var i = 0; i < configListeners.length; i++) configListeners[i]();
    }
    function getConfig() {
      return configState;
    }
    function setConfig(patch) {
      configState = mergeConfig(Object.assign({}, configState, patch));
      storageSet(STORAGE_KEY, JSON.stringify(configState));
      notifyConfig();
    }
    function setStateConfig(key, patch) {
      var states = Object.assign({}, configState.states);
      states[key] = Object.assign({}, states[key], patch);
      setConfig({ states: states });
    }
    function setStateColor(key, index, color) {
      var colors = configState.states[key].colors.slice();
      colors[index] = color;
      setStateConfig(key, { colors: colors });
    }
    function addStateColor(key) {
      var colors = configState.states[key].colors.slice();
      if (colors.length >= MAX_COLORS) return;
      colors.push("#ffffff");
      setStateConfig(key, { colors: colors });
    }
    function removeStateColor(key, index) {
      var colors = configState.states[key].colors.slice();
      if (colors.length <= 1) return;
      colors.splice(index, 1);
      setStateConfig(key, { colors: colors });
    }
    function resetConfig() {
      configState = mergeConfig(DEFAULT_CONFIG);
      storageSet(STORAGE_KEY, JSON.stringify(configState));
      notifyConfig();
    }
    function subscribeConfig(fn) {
      configListeners.push(fn);
      return function () {
        var i = configListeners.indexOf(fn);
        if (i >= 0) configListeners.splice(i, 1);
      };
    }
    /** 初始化：localStorage（用户设置） > cordis 行 config（部署默认） > 内置默认。 */
    function initConfig(rowConfig) {
      var stored = readStoredConfig();
      configState = mergeConfig(stored !== null ? stored : rowConfig);
      if (stored === null) storageSet(STORAGE_KEY, JSON.stringify(configState));
    }

    /* ============================================================
     *  五、状态解析：DSH 会话摘要 -> 灯光模式
     * ============================================================ */
    /**
     * 会话列表选择器：把整份会话快照收成一个「本条灯光该按什么状态显示」的结论。
     *
     * 平台事实（两处都别写回旧写法）：
     *   - shell.overlay 拿到的 useSessions 快照是 SessionListState：
     *     { ids, byId, phase, projectionsBySession }，**没有 current 字段**。
     *     旧实现读 sessions.current，恒为 undefined，于是永远落在 idle。
     *   - pendingInteraction（审批 / 计划确认 / 提问）不在 SessionSummary 上，
     *     它在 useSessionStatus 的 Map<SessionId, {running, pendingInteraction, completionUnread}> 里
     *     （completionUnread 只在子会话上置位，实际看不到，本插件不消费）。
     *     useSessionStatus 与 useSessions 一样是 root 级全局标准 props。
     *
     * 所以这里不挑「当前会话」——root 作用域拿不到那个选择——而是按优先级汇总所有
     * 会话：有交互待处理 > 有在跑 > 全空白/无会话 > 其余算完成。
     * 待处理再按 kind 细分：approval（等审批）/ questioning（等回答·计划确认），
     * 认不出的 kind 落 approval 兜底。
     */
    function deriveMode(sessions, statuses, config) {
      if (!config || config.enabled !== true) return null;
      var byId = (sessions && sessions.byId) || {};
      var ids = (sessions && sessions.ids) || [];
      var list = [];
      var i;
      /* id 必须取自 ids / byId 的键本身：行对象上的 id 不保证存在（测试与宿主
         投影都只保证 byId 的键是 id），从行上读会让 useSessionStatus 查不到。 */
      for (i = 0; i < ids.length; i++) {
        var row = byId[ids[i]];
        if (row !== undefined && row !== null) list.push({ id: ids[i], row: row });
      }
      /* 没有 ids 时兜底扫 byId，兼容将来快照只带 byId 的情况 */
      if (!list.length) {
        for (var id in byId) {
          if (Object.prototype.hasOwnProperty.call(byId, id) && byId[id]) list.push({ id: id, row: byId[id] });
        }
      }
      if (!list.length) return "idle";

      var anyRunning = false;
      var anyNonBlank = false;
      for (i = 0; i < list.length; i++) {
        var entry = list[i];
        var summary = entry.row;
        var status = statuses && typeof statuses.get === "function" ? statuses.get(entry.id) : undefined;
        if (status && status.pendingInteraction !== undefined && status.pendingInteraction !== null) {
          return stateFromInteractionKind(status.pendingInteraction.kind);
        }
        if (summary.running === true || (status && status.running === true)) anyRunning = true;
        if (summary.blank !== true) anyNonBlank = true;
      }
      /* 正在跑的会话优先于空闲 / 完成判定——灯先反映当下在忙 */
      if (anyRunning) return "running";
      if (!anyNonBlank) return "idle";
      return "success";
    }

    /**
     * 单个会话摘要 -> 灯光模式（deriveMode 的单元，保留给测试与二次开发）。
     *   running > 空白会话=空闲 > 完成；没有摘要按空闲处理（物理灯放在桌上，
     *   这是它的常态）。enabled=false 时返回 null（由 buildDesired 转成关灯）。
     */
    function resolveMode(summary, config) {
      if (!config || config.enabled !== true) return null;
      if (summary === undefined || summary === null) return "idle";
      if (summary.running === true) return "running";
      if (summary.blank === true) return "idle";
      return "success";
    }

    /* ============================================================
     *  六、目标值 & 差量写入计划（纯函数，便于测试）
     * ============================================================ */
    /**
     * 某个 DSH 状态对应的「灯具目标值」。
     * 返回 null 表示不改动灯具；返回 { power: 0 } 表示只关灯。
     */
    function targetFromState(stateKey, config) {
      var st = config.states[stateKey];
      if (!st || st.mode === "off") {
        return config.autoPower === false ? null : { power: 0 };
      }
      var target = {
        brightness: clampInt(st.brightness, 0, 100, 50),
        mode: MODE_CODE[st.mode] === undefined ? 0 : MODE_CODE[st.mode],
        color: hexToRgb(st.colors[0]),
        effectSpeed: clampInt(st.speed, 0, 100, 30)
      };
      if (config.autoPower !== false) target.power = 1;
      return target;
    }

    /**
     * 差量写入计划：只写和上次成功下发的值不同的特征，且顺序固定为
     * 颜色 -> 亮度 -> 速度 -> 模式 -> 电源，电源放最后，避免中间态闪一下。
     */
    function planWrites(prev, target) {
      if (!target) return [];
      var p = prev || {};
      var ops = [];
      if (target.color && !sameBytes(p.color, target.color)) {
        ops.push({ uuid: CH.color, bytes: target.color.slice() });
      }
      if (target.brightness !== undefined && target.brightness !== p.brightness) {
        ops.push({ uuid: CH.brightness, bytes: [target.brightness] });
      }
      if (target.effectSpeed !== undefined && target.effectSpeed !== p.effectSpeed) {
        ops.push({ uuid: CH.effectSpeed, bytes: [target.effectSpeed] });
      }
      if (target.mode !== undefined && target.mode !== p.mode) {
        ops.push({ uuid: CH.mode, bytes: [target.mode] });
      }
      if (target.power !== undefined && target.power !== p.power) {
        ops.push({ uuid: CH.power, bytes: [target.power ? 1 : 0] });
      }
      return ops;
    }

    /**
     * 模式 + 配置 -> 本次要下发的完整期望：
     *   { mode, target, marquee, countdown, gapMs, reconnect }
     * marquee 非空时由链路层按 intervalMs 轮播 colors（0xFFF4）。
     */
    function buildDesired(mode, config) {
      var gapMs = config.writeGapMs;
      var base = { mode: mode, gapMs: gapMs, reconnect: config.reconnect !== false };
      if (!config.enabled) {
        /* 停用 = 关灯并取消倒计时 */
        return Object.assign(base, {
          target: config.autoPower === false ? null : { power: 0 },
          marquee: null,
          countdown: 0
        });
      }
      if (mode === null || mode === undefined) {
        return Object.assign(base, { target: null, marquee: null, countdown: 0 });
      }
      var st = config.states[mode];
      var target = targetFromState(mode, config);
      var marquee = null;
      if (st && st.mode === "cycle" && st.colors.length > 1) {
        marquee = { colors: st.colors.slice(), intervalMs: st.intervalMs };
      }
      /* 空闲自动关灯：只有真正空闲（或勾选了「完成也计时」）才启动灯内倒计时 */
      var countdown = 0;
      var idleLike = mode === "idle" || (config.idleAutoOffIncludeSuccess && mode === "success");
      if (idleLike && config.idleAutoOffEnabled && st && st.mode !== "off") {
        countdown = countdownSeconds(config.idleAutoOffMinutes);
      }
      return Object.assign(base, { target: target, marquee: marquee, countdown: countdown });
    }

    /* ============================================================
     *  七、蓝牙链路（Web Bluetooth）
     * ============================================================ */
    /**
     * 链路管理器。所有 GATT 操作串行化在一个 Promise 队列里，避免
     * "GATT operation already in progress"；写入做差量，写间隔可配。
     */
    function createLink(options) {
      var opts = options || {};
      var nav = opts.bluetooth !== undefined
        ? opts.bluetooth
        : (typeof navigator !== "undefined" ? navigator.bluetooth : undefined);
      var reconnectLimit = num(opts.reconnectLimit, 6);

      var chars = {};             /* 0xfff1 -> BluetoothRemoteGATTCharacteristic */
      var device = null;
      var queue = Promise.resolve();
      var disposed = false;
      var wantConnection = false; /* 用户/自动重连的意图；显式断开时置 false */
      var lastApplied = null;     /* 上次成功下发的目标值（差量基准） */
      var lastCountdown = null;
      var desired = null;
      var reconnectTimer = null;
      var reconnectAttempts = 0;
      var pollTimer = null;
      var marqueeTimer = null;
      var marqueeIndex = 0;
      var marqueeSpec = null;
      var listeners = [];

      var status = {
        available: !!(nav && nav.requestDevice),
        connected: false,
        connecting: false,
        deviceName: "",
        battery: null,
        countdown: 0,
        error: "",
        mode: null
      };

      function statusSnapshot() {
        return status;
      }
      function patch(next) {
        var changed = false;
        for (var k in next) {
          if (Object.prototype.hasOwnProperty.call(next, k) && status[k] !== next[k]) {
            status[k] = next[k];
            changed = true;
          }
        }
        if (!changed) return;
        for (var i = 0; i < listeners.length; i++) {
          try {
            listeners[i](status);
          } catch (e) {
            /* 单个订阅者出错不影响其他人 */
          }
        }
      }
      function subscribe(fn) {
        listeners.push(fn);
        return function () {
          var i = listeners.indexOf(fn);
          if (i >= 0) listeners.splice(i, 1);
        };
      }
      function sleep(ms) {
        return new Promise(function (resolve) {
          setTimeout(resolve, ms);
        });
      }
      /** 串行队列：保证任意时刻只有一个 GATT 操作在飞。 */
      function run(fn) {
        var result = queue.then(function () {
          return fn();
        });
        queue = result.then(function () {}, function () {});
        return result;
      }

      function rememberDevice(dev) {
        if (!dev) return;
        storageSet(DEVICE_KEY, JSON.stringify({ id: dev.id || "", name: dev.name || "" }));
      }
      function readRemembered() {
        var raw = storageGet(DEVICE_KEY);
        if (!raw) return null;
        try {
          return JSON.parse(raw);
        } catch (e) {
          return null;
        }
      }

      /* ---------- 特征发现 ---------- */
      function collectChars(service) {
        var wanted = Object.keys(CH).map(function (k) { return CH[k]; });
        function allAtOnce() {
          if (typeof service.getCharacteristics !== "function") {
            return Promise.reject(new Error("getCharacteristics 不可用"));
          }
          return service.getCharacteristics().then(function (list) {
            var map = {};
            for (var i = 0; i < list.length; i++) {
              var code = shortUuid(list[i].uuid);
              if (code !== null) map[code] = list[i];
            }
            return map;
          });
        }
        function oneByOne() {
          return Promise.all(wanted.map(function (code) {
            if (typeof service.getCharacteristic !== "function") return Promise.resolve(null);
            return service.getCharacteristic(code).then(function (c) {
              return [code, c];
            }, function () {
              return null;
            });
          })).then(function (pairs) {
            var map = {};
            for (var i = 0; i < pairs.length; i++) {
              if (pairs[i]) map[pairs[i][0]] = pairs[i][1];
            }
            return map;
          });
        }
        return allAtOnce().catch(oneByOne).then(function (map) {
          chars = map;
          return map;
        });
      }

      function watchBattery(c) {
        if (!c || !c.properties || !c.properties.notify) return;
        try {
          c.addEventListener("characteristicvaluechanged", function (ev) {
            var dv = ev && ev.target ? ev.target.value : null;
            if (dv && typeof dv.getUint8 === "function") patch({ battery: dv.getUint8(0) });
          });
          var started = c.startNotifications();
          if (started && typeof started.catch === "function") started.catch(function () {});
        } catch (e) {
          /* 电量通知是锦上添花，失败不影响主流程 */
        }
      }

      function readBattery() {
        var c = chars[CH.battery];
        if (!c || typeof c.readValue !== "function") return Promise.resolve();
        return run(function () {
          return c.readValue();
        }).then(function (dv) {
          if (dv && typeof dv.getUint8 === "function") patch({ battery: dv.getUint8(0) });
        }, function () {});
      }

      /* ---------- 空闲倒计时：轮询灯具实际剩余秒数，用于界面显示 ---------- */
      function stopCountdownPoll() {
        if (pollTimer) {
          clearInterval(pollTimer);
          pollTimer = null;
        }
      }
      function readCountdown() {
        var c = chars[CH.countdown];
        if (!c || typeof c.readValue !== "function") return Promise.resolve();
        return run(function () {
          return c.readValue();
        }).then(function (dv) {
          if (dv && typeof dv.getUint16 === "function") patch({ countdown: dv.getUint16(0, true) });
        }, function () {});
      }
      function syncCountdownPoll(seconds) {
        stopCountdownPoll();
        if (!seconds || !status.connected) return;
        pollTimer = setInterval(function () {
          if (!status.connected) {
            stopCountdownPoll();
            return;
          }
          readCountdown();
        }, 5000);
      }

      /* ---------- 软件跑马灯（多色轮播） ---------- */
      function stopMarquee() {
        if (marqueeTimer) {
          clearInterval(marqueeTimer);
          marqueeTimer = null;
        }
        marqueeSpec = null;
        marqueeIndex = 0;
      }
      function writeColor(rgb) {
        var c = chars[CH.color];
        if (!c) return Promise.resolve();
        lastApplied = Object.assign({}, lastApplied, { color: rgb.slice() });
        return run(function () {
          return c.writeValue(new Uint8Array(rgb));
        }).then(function () {}, function (err) {
          patch({ error: describeError(err) });
        });
      }
      function syncMarquee(spec) {
        var same = false;
        if (marqueeSpec && spec) {
          same =
            marqueeSpec.intervalMs === spec.intervalMs &&
            sameBytes(
              marqueeSpec.colors.map(hexToRgb).reduce(function (a, b) { return a.concat(b); }, []),
              spec.colors.map(hexToRgb).reduce(function (a, b) { return a.concat(b); }, [])
            );
        }
        if (same && marqueeTimer) return;
        stopMarquee();
        if (!spec || spec.colors.length < 2 || !status.connected) return;
        marqueeSpec = spec;
        marqueeIndex = 0;
        marqueeTimer = setInterval(function () {
          if (!status.connected || !marqueeSpec) {
            stopMarquee();
            return;
          }
          marqueeIndex = (marqueeIndex + 1) % marqueeSpec.colors.length;
          writeColor(hexToRgb(marqueeSpec.colors[marqueeIndex]));
        }, Math.max(MIN_MARQUEE_MS, spec.intervalMs));
      }

      /* ---------- 写入 ---------- */
      /** 固件缺 0xFFFA 时只提示一次，不反复报错。 */
      var countdownWarned = false;
      function noteCountdownUnsupported() {
        if (countdownWarned || hasCountdownChar()) return;
        countdownWarned = true;
        patch({ error: "固件没有 0xFFFA 倒计时特征：灯效正常，但「空闲自动关灯」不可用（需重烧固件）" });
      }
      /**
       * 串行下发一组特征写入。
       *
       * 区分两类操作，这是「关灯」以前会失效的原因：
       *   - 必需（默认）：颜色 / 亮度 / 速度 / 模式 / 电源。任一失败就抛出，
       *     由调用方报错。
       *   - 可选（op.optional === true）：0xFFFA 倒计时这类「有更好、没有也能用」
       *     的写入。失败只记下来，**继续执行后面的必需写入**。
       *
       * 早先的实现把倒计时和电源顺序放在同一条链上，旧固件没有 0xFFFA 时
       * 倒计时那一步先抛错，后面的 power=0 就永远发不出去——看起来就是
       * 「关灯没反应」。所以倒计时一律按可选处理。
       */
      function applyOps(ops, gapMs) {
        if (!ops || !ops.length) return Promise.resolve();
        return run(function () {
          var chain = Promise.resolve();
          var optionalFailure = null;
          ops.forEach(function (op, index) {
            chain = chain.then(function () {
              var optional = op.optional === true;
              function note(run) {
                return run.then(function () {}, function (err) {
                  if (optional) {
                    if (!optionalFailure) optionalFailure = err;
                    return undefined;
                  }
                  throw err;
                });
              }
              var c = chars[op.uuid];
              if (!c) {
                return note(Promise.reject(new Error(
                  "特征 " + hex4(op.uuid) + " 不可用，请确认固件已包含灯控服务"
                )));
              }
              var write = function () {
                return c.writeValue(new Uint8Array(op.bytes));
              };
              /* 第一条不等间隔，其余按 writeGapMs 拉开，避免协议栈排队 */
              if (index === 0 || !gapMs) return note(write());
              return note(sleep(gapMs).then(write));
            });
          });
          return chain.then(function () {
            return optionalFailure;
          });
        });
      }

      /** 固件是否带 0xFFFA 倒计时特征（只在连上后判断）。 */
      function hasCountdownChar() {
        return !!chars[CH.countdown];
      }

      /** 把当前期望下发到灯具（差量）。未连接时只记录，连上后 flush 自动补发。 */
      function flush() {
        if (!status.connected || !desired) return Promise.resolve();
        var gapMs = desired.gapMs || 0;
        var ops = planWrites(lastApplied, desired.target);
        var wantCountdown = desired.countdown || 0;
        var countdownChanged = wantCountdown !== lastCountdown;
        /* 本次要写的秒数必须在改写 lastCountdown 之前取好 */
        var countdownBytes = uint16le(wantCountdown);
        return applyOps(ops, gapMs).then(function () {
          /* 状态写入成功才更新差量基准；失败时保留原基准，下次重试 */
          try {
            if (ops.length && desired.target) lastApplied = desired.target;
          } catch (e) {}
          if (!countdownChanged) return undefined;
          /* 倒计时是可选能力：写不进去（旧固件没有 0xFFFA）也不能影响灯效 */
          noteCountdownUnsupported();
          return applyOps([{ uuid: CH.countdown, bytes: countdownBytes, optional: true }], gapMs)
            .then(function () {
              lastCountdown = wantCountdown;
              syncCountdownPoll(wantCountdown);
            });
        }).then(function () {
          syncMarquee(desired.marquee);
        }).catch(function (err) {
          patch({ error: describeError(err) });
        });
      }

      function apply(next) {
        desired = next || null;
        patch({ mode: next ? next.mode : null });
        return flush();
      }

      /**
       * 手动关灯：取消灯内倒计时 + 断电。
       * 倒计时按可选处理——旧固件没有 0xFFFA 时，power=0 仍必须发出去，
       * 否则「关灯」按钮会完全没反应。
       */
      function powerOff() {
        if (!status.connected) return Promise.resolve();
        var gapMs = desired ? desired.gapMs : 0;
        stopMarquee();
        stopCountdownPoll();
        patch({ countdown: 0, mode: null });
        noteCountdownUnsupported();
        return applyOps([
          { uuid: CH.countdown, bytes: [0, 0], optional: true },
          { uuid: CH.power, bytes: [0] }
        ], gapMs).then(function () {
          lastApplied = { power: 0 };
          lastCountdown = 0;
        }).catch(function (err) {
          patch({ error: describeError(err) });
        });
      }

      /* ---------- 连接 / 断开 / 重连 ---------- */
      function attachHandlers(dev) {
        try {
          dev.removeEventListener("gattserverdisconnected", onDisconnected);
        } catch (e) {}
        try {
          dev.addEventListener("gattserverdisconnected", onDisconnected);
        } catch (e) {}
      }

      function onDisconnected(ev) {
        /* 换设备时旧设备会先触发一次断开，忽略它，别把新连接搅乱 */
        var source = ev && ev.target ? ev.target : device;
        if (device && source && source !== device) return;
        chars = {};
        lastApplied = null;
        lastCountdown = null;
        countdownWarned = false;
        stopMarquee();
        stopCountdownPoll();
        patch({ connected: false, connecting: false, battery: null, countdown: 0 });
        if (wantConnection && !disposed && (!desired || desired.reconnect !== false)) {
          scheduleReconnect();
        }
      }

      function scheduleReconnect() {
        if (reconnectTimer || disposed) return;
        if (reconnectAttempts >= reconnectLimit) {
          patch({ error: "自动重连失败，请点「连接设备」重新选择" });
          return;
        }
        var delay = Math.min(1000 * Math.pow(2, reconnectAttempts), 15000);
        reconnectAttempts += 1;
        reconnectTimer = setTimeout(function () {
          reconnectTimer = null;
          if (!wantConnection || disposed || !device) return;
          patch({ connecting: true });
          connectGatt(device).catch(function () {
            patch({ connected: false, connecting: false });
            scheduleReconnect();
          });
        }, delay);
      }

      function connectGatt(dev) {
        if (!dev || !dev.gatt) return Promise.reject(new Error("设备不可用"));
        /* 换设备：先把旧连接断掉，避免同时占着两路 GATT */
        if (device && device !== dev && device.gatt && device.gatt.connected) {
          try {
            device.gatt.disconnect();
          } catch (e) {}
        }
        return Promise.resolve(dev.gatt.connect())
          .then(function (server) {
            if (!server) throw new Error("GATT 连接失败");
            return server.getPrimaryService(SERVICE_UUID);
          })
          .then(function (service) {
            return collectChars(service);
          })
          .then(function (map) {
            device = dev;
            rememberDevice(dev);
            attachHandlers(dev);
            reconnectAttempts = 0;
            lastApplied = null;   /* 重连后灯具状态未知，重新全量下发 */
            lastCountdown = null;
            patch({
              connected: true,
              connecting: false,
              deviceName: dev.name || DEFAULT_DEVICE_NAME,
              error: "",
              countdown: 0
            });
            var batteryChar = map[CH.battery];
            if (batteryChar) watchBattery(batteryChar);
            readBattery();
            return flush();
          });
      }

      /** 用户手势触发：弹出设备选择框（必须在点击事件里同步调用 requestDevice）。 */
      function connectNew() {
        if (!status.available) {
          patch({ error: "当前环境不支持 Web Bluetooth：请用 Chrome/Edge，并通过 https 或 localhost 打开" });
          return Promise.resolve();
        }
        var request;
        try {
          request = nav.requestDevice({
            filters: [{ services: [SERVICE_UUID] }, { namePrefix: DEFAULT_DEVICE_NAME }],
            optionalServices: [SERVICE_UUID]
          });
        } catch (err) {
          patch({ error: describeError(err) });
          return Promise.resolve();
        }
        wantConnection = true;
        patch({ connecting: true, error: "" });
        return request.then(function (dev) {
          return connectGatt(dev);
        }).catch(function (err) {
          /* 用户在选择框里点了取消：已经连上的设备不该因此失去自动重连 */
          wantConnection = status.connected === true;
          patch({ connecting: false, error: describeError(err) });
        });
      }

      /** 免手势重连：复用浏览器已授权的设备（Chrome/Edge 的 getDevices）。 */
      function reconnectLast(silent) {
        if (!status.available) {
          if (!silent) patch({ error: "当前环境不支持 Web Bluetooth" });
          return Promise.resolve();
        }
        if (status.connected || status.connecting) return queue;
        if (typeof nav.getDevices !== "function") {
          if (!silent) patch({ error: "当前浏览器不支持免选择重连，请点「连接设备」" });
          return Promise.resolve();
        }
        var saved = readRemembered();
        wantConnection = true;
        patch({ connecting: true, error: "" });
        return nav.getDevices().then(function (list) {
          var dev = null;
          var i;
          if (saved && saved.id) {
            for (i = 0; i < list.length; i++) {
              if (list[i].id === saved.id) {
                dev = list[i];
                break;
              }
            }
          }
          if (!dev && list.length) dev = list[0];
          if (!dev) {
            wantConnection = false;
            patch({ connecting: false, error: silent ? "" : "还没有授权过的设备，请点「连接设备」" });
            return undefined;
          }
          return connectGatt(dev);
        }).catch(function (err) {
          patch({ connecting: false, error: silent ? "" : describeError(err) });
        });
      }

      function disconnect() {
        wantConnection = false;
        if (reconnectTimer) {
          clearTimeout(reconnectTimer);
          reconnectTimer = null;
        }
        stopMarquee();
        stopCountdownPoll();
        patch({ connecting: false, connected: false, countdown: 0, battery: null });
        try {
          if (device && device.gatt && device.gatt.connected) device.gatt.disconnect();
        } catch (e) {}
        return Promise.resolve();
      }

      function dispose() {
        disposed = true;
        wantConnection = false;
        if (reconnectTimer) {
          clearTimeout(reconnectTimer);
          reconnectTimer = null;
        }
        stopMarquee();
        stopCountdownPoll();
        listeners = [];
        try {
          if (device) device.removeEventListener("gattserverdisconnected", onDisconnected);
        } catch (e) {}
        try {
          if (device && device.gatt && device.gatt.connected) device.gatt.disconnect();
        } catch (e) {}
      }

      /* 清掉当前错误提示（设置页的「清除提示」按钮用） */
      function clearError() {
        if (status.error) patch({ error: "" });
      }

      return {
        status: statusSnapshot,
        subscribe: subscribe,
        connectNew: connectNew,
        reconnectLast: reconnectLast,
        disconnect: disconnect,
        apply: apply,
        powerOff: powerOff,
        clearError: clearError,
        dispose: dispose,
        /* 便于测试 / 调试 */
        chars: function () { return chars; },
        desired: function () { return desired; },
        lastApplied: function () { return lastApplied; }
      };
    }

    /* ============================================================
     *  八、链路单例（在 apply 里创建，随插件卸载销毁）
     * ============================================================ */
    var link = null;
    var EMPTY_STATUS = {
      available: false,
      connected: false,
      connecting: false,
      deviceName: "",
      battery: null,
      countdown: 0,
      error: "",
      mode: null
    };
    function getLink() {
      return link;
    }
    function callLink(method) {
      var l = link;
      if (!l || typeof l[method] !== "function") return undefined;
      return l[method].apply(l, Array.prototype.slice.call(arguments, 1));
    }

    /* ============================================================
     *  九、样式（一次性注入）
     * ============================================================ */
    function ensureStyles() {
      if (typeof document === "undefined") return;
      if (document.getElementById(STYLE_ID)) return;
      var tag = document.createElement("style");
      tag.id = STYLE_ID;
      tag.dataset.plugin = "dsh-ble-light";
      tag.textContent = [
        /* --- 左下角状态角标 --- */
        ".dsh-ble-light-pill {",
        "  position: fixed;",
        "  left: 10px;",
        "  bottom: 10px;",
        "  z-index: 2147483000;",
        "  pointer-events: none;",
        "  display: flex;",
        "  align-items: center;",
        "  gap: 6px;",
        "  padding: 3px 9px;",
        "  border-radius: 999px;",
        "  font-size: 11px;",
        "  line-height: 1.5;",
        "  font-family: inherit;",
        "  color: #fff;",
        "  background: rgba(0, 0, 0, 0.55);",
        "  opacity: 0.8;",
        "}",
        ".dsh-ble-light-pill i {",
        "  width: 7px;",
        "  height: 7px;",
        "  border-radius: 50%;",
        "  background: #9ca3af;",
        "  flex: none;",
        "}",
        ".dsh-ble-light-pill[data-on=\"1\"] i {",
        "  background: #22c55e;",
        "  box-shadow: 0 0 6px rgba(34, 197, 94, 0.9);",
        "}",
        ".dsh-ble-light-pill[data-busy=\"1\"] i {",
        "  background: #f59e0b;",
        "  animation: dsh-ble-light-blink 1s ease-in-out infinite;",
        "}",
        "@keyframes dsh-ble-light-blink {",
        "  0%, 100% { opacity: 1; }",
        "  50% { opacity: 0.3; }",
        "}",
        "@media (prefers-reduced-motion: reduce) {",
        "  .dsh-ble-light-pill[data-busy=\"1\"] i { animation: none; }",
        "}",
        /* --- 设置页 --- */
        ".dsh-ble-light-settings {",
        "  display: flex;",
        "  flex-direction: column;",
        "  gap: 14px;",
        "  max-width: 520px;",
        "  padding: 4px 0 16px;",
        "}",
        ".dsh-ble-light-settings h2 {",
        "  margin: 0;",
        "  font-size: 15px;",
        "  line-height: 1.4;",
        "  color: var(--dsw-alias-label-primary, #1f2329);",
        "}",
        ".dsh-ble-light-settings h3 {",
        "  margin: 0;",
        "  font-size: 13px;",
        "  font-weight: 600;",
        "  color: var(--dsw-alias-label-primary, #1f2329);",
        "}",
        ".dsh-ble-light-settings .bl-hint {",
        "  margin: -6px 0 0;",
        "  font-size: 12px;",
        "  line-height: 1.6;",
        "  color: var(--dsw-alias-label-secondary, #646a73);",
        "}",
        ".dsh-ble-light-settings .bl-error {",
        "  margin: 0;",
        "  font-size: 12px;",
        "  line-height: 1.6;",
        "  color: #d4380d;",
        "  word-break: break-all;",
        "}",
        ".dsh-ble-light-settings .bl-card {",
        "  display: flex;",
        "  flex-direction: column;",
        "  gap: 9px;",
        "  padding: 10px 12px;",
        "  border: 1px solid var(--dsw-alias-border-l2-darkmode-thin, rgba(0, 0, 0, 0.12));",
        "  border-radius: 10px;",
        "}",
        ".dsh-ble-light-settings .bl-card-head {",
        "  display: flex;",
        "  align-items: center;",
        "  justify-content: space-between;",
        "  gap: 10px;",
        "}",
        ".dsh-ble-light-settings .bl-conn {",
        "  display: flex;",
        "  align-items: center;",
        "  gap: 6px;",
        "  font-size: 13px;",
        "  color: var(--dsw-alias-label-secondary, #646a73);",
        "}",
        ".dsh-ble-light-settings .bl-conn i {",
        "  width: 8px;",
        "  height: 8px;",
        "  border-radius: 50%;",
        "  background: #9ca3af;",
        "  flex: none;",
        "}",
        ".dsh-ble-light-settings .bl-conn.is-on i {",
        "  background: #22c55e;",
        "  box-shadow: 0 0 6px rgba(34, 197, 94, 0.9);",
        "}",
        ".dsh-ble-light-settings .bl-field {",
        "  display: flex;",
        "  align-items: center;",
        "  justify-content: space-between;",
        "  gap: 12px;",
        "}",
        ".dsh-ble-light-settings .bl-label {",
        "  font-size: 13px;",
        "  color: var(--dsw-alias-label-secondary, #646a73);",
        "}",
        ".dsh-ble-light-settings .bl-controls {",
        "  display: flex;",
        "  align-items: center;",
        "  gap: 8px;",
        "  flex-wrap: wrap;",
        "  justify-content: flex-end;",
        "}",
        ".dsh-ble-light-settings input[type=\"range\"] {",
        "  width: 170px;",
        "  accent-color: var(--dsw-brand-color-6, #1668dc);",
        "}",
        ".dsh-ble-light-settings input[type=\"checkbox\"] {",
        "  width: 16px;",
        "  height: 16px;",
        "  accent-color: var(--dsw-brand-color-6, #1668dc);",
        "}",
        ".dsh-ble-light-settings input[type=\"color\"] {",
        "  width: 34px;",
        "  height: 26px;",
        "  padding: 0;",
        "  border: 1px solid var(--dsw-alias-border-l2-darkmode-thin, rgba(0, 0, 0, 0.12));",
        "  border-radius: 6px;",
        "  background: transparent;",
        "  cursor: pointer;",
        "}",
        ".dsh-ble-light-settings select {",
        "  padding: 4px 8px;",
        "  font-size: 13px;",
        "  border: 1px solid var(--dsw-alias-border-l2-darkmode-thin, rgba(0, 0, 0, 0.12));",
        "  border-radius: 6px;",
        "  background: var(--dsw-specific-input-major, #ffffff);",
        "  color: var(--dsw-alias-label-primary, #1f2329);",
        "}",
        ".dsh-ble-light-settings .bl-swatch {",
        "  width: 100%;",
        "  height: 10px;",
        "  border-radius: 6px;",
        "  border: 1px solid var(--dsw-alias-border-l2-darkmode-thin, rgba(0, 0, 0, 0.12));",
        "}",
        ".dsh-ble-light-settings button {",
        "  padding: 5px 12px;",
        "  font-size: 13px;",
        "  border-radius: 8px;",
        "  cursor: pointer;",
        "  border: 1px solid var(--dsw-alias-border-l2-darkmode-thin, rgba(0, 0, 0, 0.12));",
        "  background: var(--dsw-specific-input-major, #ffffff);",
        "  color: var(--dsw-alias-label-primary, #1f2329);",
        "}",
        ".dsh-ble-light-settings button:disabled {",
        "  opacity: 0.45;",
        "  cursor: not-allowed;",
        "}",
        ".dsh-ble-light-settings button.bl-primary {",
        "  border-color: var(--dsw-brand-color-6, #1668dc);",
        "  background: var(--dsw-brand-color-6, #1668dc);",
        "  color: #ffffff;",
        "}",
        ".dsh-ble-light-settings button.bl-mini {",
        "  padding: 2px 8px;",
        "  font-size: 12px;",
        "  border-radius: 6px;",
        "}",
        ".dsh-ble-light-settings .bl-actions {",
        "  display: flex;",
        "  flex-wrap: wrap;",
        "  gap: 8px;",
        "  justify-content: flex-end;",
        "  margin-top: 4px;",
        "}"
      ].join("\n");
      document.head.appendChild(tag);
    }

    /* ============================================================
     *  十、React hooks
     * ============================================================ */
    function useConfig() {
      var force = React.useState(0)[1];
      React.useEffect(function () {
        return subscribeConfig(function () {
          force(function (n) { return n + 1; });
        });
      }, []);
      return getConfig();
    }

    function useLinkStatus() {
      var force = React.useState(0)[1];
      React.useEffect(function () {
        var l = getLink();
        if (!l) return undefined;
        return l.subscribe(function () {
          force(function (n) { return n + 1; });
        });
      }, []);
      var l = getLink();
      return l ? l.status() : EMPTY_STATUS;
    }

    /* ============================================================
     *  十一、状态桥：会话状态 -> 蓝牙灯（shell.overlay）
     * ============================================================ */
    function useNoSessions() {
      return undefined;
    }
    function useNoSessionStatus() {
      return undefined;
    }
    /* 选择函数必须是稳定引用，否则每次渲染都会让选择器快照失效 */
    function selectSessionList(sessions) {
      return sessions;
    }
    function selectStatusMap(statuses) {
      return statuses;
    }

    function Bridge(props) {
      var config = useConfig();
      var status = useLinkStatus();
      /* 两个都是 root 级全局标准 props：会话列表 + 会话 UI 状态（含待处理） */
      var subscribeSessions =
        props && typeof props.useSessions === "function" ? props.useSessions : useNoSessions;
      var subscribeSessionStatus =
        props && typeof props.useSessionStatus === "function" ? props.useSessionStatus : useNoSessionStatus;
      var mode = deriveMode(subscribeSessions(selectSessionList), subscribeSessionStatus(selectStatusMap), config);

      /* 模式 / 配置 / 连接状态变化时把期望值推给灯（差量，重复调用无副作用） */
      React.useEffect(function () {
        callLink("apply", buildDesired(mode, config));
      }, [mode, config, status.connected]);

      /* 首次挂载（或重新打开自动重连）时静默恢复上次的设备 */
      React.useEffect(function () {
        if (config.autoConnect) callLink("reconnectLast", true);
      }, [config.autoConnect]);

      /* 卸载时停止驱动，别让定时器留在页面里空转 */
      React.useEffect(function () {
        return function () {
          callLink("apply", null);
        };
      }, []);

      if (!config.showStatus || !config.enabled) return null;

      var text = "蓝牙灯";
      if (status.connected) {
        text += " · " + (mode ? STATE_LABELS[mode] : "已连接");
        if (status.battery !== null && status.battery !== undefined) text += " · " + status.battery + "%";
        if (status.countdown > 0) text += " · 倒计时 " + formatRemain(status.countdown);
      } else if (status.connecting) {
        text += " · 连接中…";
      } else {
        text += " · 未连接";
      }

      return h(
        "div",
        {
          className: "dsh-ble-light-pill",
          "data-on": status.connected ? "1" : "0",
          "data-busy": status.connecting ? "1" : "0",
          "aria-hidden": true,
          title: status.error || text
        },
        h("i", null),
        h("span", null, text)
      );
    }

    /* ============================================================
     *  十二、设置页（settings.section）
     * ============================================================ */
    function SliderField(props) {
      return h(
        "label",
        { className: "bl-field" },
        h("span", { className: "bl-label" }, props.label),
        h(
          "span",
          { className: "bl-controls" },
          h("input", {
            type: "range",
            min: props.min,
            max: props.max,
            step: props.step,
            value: props.value,
            onChange: function (ev) {
              props.onChange(parseFloat(ev.target.value));
            }
          }),
          h("span", { className: "bl-label" }, props.display)
        )
      );
    }

    function ToggleField(props) {
      return h(
        "label",
        { className: "bl-field" },
        h("span", { className: "bl-label" }, props.label),
        h("input", {
          type: "checkbox",
          checked: props.checked === true,
          onChange: function (ev) {
            props.onChange(ev.target.checked);
          }
        })
      );
    }

    function SelectField(props) {
      return h(
        "div",
        { className: "bl-field" },
        h("span", { className: "bl-label" }, props.label),
        h(
          "select",
          {
            value: props.value,
            onChange: function (ev) {
              props.onChange(ev.target.value);
            }
          },
          props.options.map(function (opt) {
            return h("option", { key: opt.value, value: opt.value }, opt.label);
          })
        )
      );
    }

    function ColorRow(props) {
      var colors = props.colors;
      var swatch = "linear-gradient(90deg, " + colors.join(", ") + ")";
      return h(
        "div",
        { className: "bl-field" },
        h("span", { className: "bl-label" }, props.label),
        h(
          "span",
          { className: "bl-controls" },
          colors.map(function (color, index) {
            return h("input", {
              key: index,
              type: "color",
              value: color,
              title: "颜色 " + (index + 1),
              onChange: function (ev) {
                setStateColor(props.stateKey, index, ev.target.value);
              }
            });
          }),
          h(
            "button",
            {
              type: "button",
              className: "bl-mini",
              disabled: colors.length >= MAX_COLORS,
              onClick: function () {
                addStateColor(props.stateKey);
              }
            },
            "+"
          ),
          h(
            "button",
            {
              type: "button",
              className: "bl-mini",
              disabled: colors.length <= 1,
              onClick: function () {
                removeStateColor(props.stateKey, colors.length - 1);
              }
            },
            "−"
          )
        )
      );
    }

    function StateCard(props) {
      var stateKey = props.stateKey;
      var st = props.state;
      var mode = st.mode;
      var hasColor = mode === "fixed" || mode === "breathing" || mode === "cycle";
      var hasSpeed = mode === "gradient" || mode === "colorful" || mode === "breathing";
      return h(
        "div",
        { className: "bl-card", "data-state": stateKey },
        h(
          "div",
          { className: "bl-card-head" },
          h("h3", null, "状态：" + STATE_LABELS[stateKey]),
          h(
            "button",
            {
              type: "button",
              className: "bl-mini",
              disabled: !props.connected,
              title: "临时套用这个灯效，直到状态变化为止",
              onClick: props.onPreview
            },
            "试灯"
          )
        ),
        h("p", { className: "bl-hint" }, STATE_HINTS[stateKey]),
        h(SelectField, {
          label: "灯效",
          value: mode,
          options: EFFECT_OPTIONS,
          onChange: function (value) {
            setStateConfig(stateKey, { mode: value });
          }
        }),
        h(ColorRow, {
          label: "颜色",
          stateKey: stateKey,
          colors: st.colors
        }),
        h("div", {
          className: "bl-swatch",
          style: { background: "linear-gradient(90deg, " + st.colors.join(", ") + ")" },
          "aria-hidden": true
        }),
        h(SliderField, {
          label: "亮度",
          min: 0,
          max: 100,
          step: 1,
          value: st.brightness,
          display: st.brightness + " / 100",
          onChange: function (v) {
            setStateConfig(stateKey, { brightness: v });
          }
        }),
        hasSpeed
          ? h(SliderField, {
              label: "速度",
              min: 0,
              max: 100,
              step: 1,
              value: st.speed,
              display: String(st.speed),
              onChange: function (v) {
                setStateConfig(stateKey, { speed: v });
              }
            })
          : null,
        mode === "cycle"
          ? h(SliderField, {
              label: "换色间隔",
              min: MIN_MARQUEE_MS,
              max: MAX_MARQUEE_MS,
              step: 50,
              value: st.intervalMs,
              display: st.intervalMs + " ms",
              onChange: function (v) {
                setStateConfig(stateKey, { intervalMs: v });
              }
            })
          : null,
        hasColor
          ? null
          : h("p", { className: "bl-hint" }, "该灯效由灯具固件用彩虹轮实现，颜色由固件决定。")
      );
    }

    function SettingsPage(props) {
      var config = useConfig();
      var status = useLinkStatus();
      var close = typeof props.close === "function" ? props.close : null;
      var busy = status.connecting === true;

      return h(
        "div",
        { className: "dsh-ble-light-settings" },
        h("h2", null, "蓝牙灯（CH591D）"),
        h(
          "p",
          { className: "bl-hint" },
          "通过 Web Bluetooth 直连 CH591D 的灯控服务（0xFFF0），把 DSH 的会话状态实时映射成灯光；" +
            "空闲时把 0.5–15 分钟写进灯具内部的 RTC 倒计时，到点由灯具自己关灯（页面关掉也生效）。"
        ),

        /* ---------- 连接 ---------- */
        h(
          "div",
          { className: "bl-card" },
          h(
            "div",
            { className: "bl-card-head" },
            h(
              "span",
              { className: "bl-conn" + (status.connected ? " is-on" : "") },
              h("i", null),
              status.connected
                ? "已连接 " + (status.deviceName || DEFAULT_DEVICE_NAME)
                : busy
                  ? "连接中…"
                  : "未连接"
            ),
            status.battery !== null && status.battery !== undefined
              ? h("span", { className: "bl-label" }, "电量 " + status.battery + "%")
              : null
          ),
          h(
            "div",
            { className: "bl-actions" },
            h(
              "button",
              {
                type: "button",
                className: "bl-primary",
                disabled: busy || !status.available,
                onClick: function () {
                  callLink("connectNew");
                }
              },
              "连接设备"
            ),
            h(
              "button",
              {
                type: "button",
                disabled: busy,
                onClick: function () {
                  callLink("reconnectLast", false);
                }
              },
              "重连上次"
            ),
            h(
              "button",
              {
                type: "button",
                disabled: !status.connected,
                onClick: function () {
                  callLink("disconnect");
                }
              },
              "断开"
            ),
            h(
              "button",
              {
                type: "button",
                disabled: !status.connected,
                onClick: function () {
                  callLink("powerOff");
                }
              },
              "关灯"
            )
          ),
          status.error
            ? h(
                "p",
                { className: "bl-error" },
                status.error,
                " ",
                h(
                  "button",
                  {
                    type: "button",
                    className: "bl-mini",
                    onClick: function () {
                      callLink("clearError");
                    }
                  },
                  "清除提示"
                )
              )
            : null,
          !status.available
            ? h(
                "p",
                { className: "bl-error" },
                "当前环境不支持 Web Bluetooth。请用 Chrome / Edge 打开，并确保地址是 https:// 或 http://localhost（DSH Web 默认 127.0.0.1 属于安全上下文）。"
              )
            : null,
          status.countdown > 0
            ? h(
                "p",
                { className: "bl-hint" },
                "空闲倒计时剩余 " + formatRemain(status.countdown) + "，到点后灯具自动关灯。"
              )
            : null
        ),

        /* ---------- 总开关 ---------- */
        h(
          "div",
          { className: "bl-card" },
          ToggleField({
            label: "启用蓝牙灯",
            checked: config.enabled,
            onChange: function (v) {
              setConfig({ enabled: v });
            }
          }),
          ToggleField({
            label: "打开页面后自动重连",
            checked: config.autoConnect,
            onChange: function (v) {
              setConfig({ autoConnect: v });
            }
          }),
          ToggleField({
            label: "断线后自动重连",
            checked: config.reconnect,
            onChange: function (v) {
              setConfig({ reconnect: v });
            }
          }),
          ToggleField({
            label: "自动开关灯（关掉后只改颜色灯效）",
            checked: config.autoPower,
            onChange: function (v) {
              setConfig({ autoPower: v });
            }
          }),
          ToggleField({
            label: "显示左下角状态角标",
            checked: config.showStatus,
            onChange: function (v) {
              setConfig({ showStatus: v });
            }
          }),
          SliderField({
            label: "写入间隔",
            min: 0,
            max: 300,
            step: 10,
            value: config.writeGapMs,
            display: config.writeGapMs + " ms",
            onChange: function (v) {
              setConfig({ writeGapMs: v });
            }
          })
        ),

        /* ---------- 空闲自动关灯 ---------- */
        h(
          "div",
          { className: "bl-card", "data-section": "idle-auto-off" },
          h("h3", null, "空闲自动关灯（灯具内部 RTC）"),
          h(
            "p",
            { className: "bl-hint" },
            "进入空闲状态时把倒计时秒数写入 0xFFFA，由灯具自己的 RTC 计时关灯；" +
              "离开空闲会立即取消倒计时，所以工作时不会被误关。"
          ),
          ToggleField({
            label: "空闲时启动倒计时",
            checked: config.idleAutoOffEnabled,
            onChange: function (v) {
              setConfig({ idleAutoOffEnabled: v });
            }
          }),
          SliderField({
            label: "空闲多久后关灯",
            min: 0.5,
            max: 15,
            step: 0.5,
            value: config.idleAutoOffMinutes,
            display: config.idleAutoOffMinutes + " 分钟（" + countdownSeconds(config.idleAutoOffMinutes) + " 秒）",
            onChange: function (v) {
              setConfig({ idleAutoOffMinutes: v });
            }
          }),
          ToggleField({
            label: "「完成」状态也计入空闲计时",
            checked: config.idleAutoOffIncludeSuccess,
            onChange: function (v) {
              setConfig({ idleAutoOffIncludeSuccess: v });
            }
          })
        ),

        /* ---------- 各状态灯效 ---------- */
        h("h3", null, "各 DSH 状态的灯光"),
        STATE_KEYS.map(function (key) {
          return h(StateCard, {
            key: key,
            stateKey: key,
            state: config.states[key],
            connected: status.connected,
            onPreview: function () {
              callLink("apply", buildDesired(key, config));
            }
          });
        }),

        /* ---------- 底部 ---------- */
        h(
          "div",
          { className: "bl-actions" },
          h(
            "button",
            {
              type: "button",
              onClick: function () {
                resetConfig();
              }
            },
            "恢复默认"
          ),
          close !== null ? h("button", { type: "button", onClick: close }, "完成") : null
        )
      );
    }

    /* ============================================================
     *  十三、插件体
     * ============================================================ */
    function apply(ctx, config) {
      ensureStyles();
      initConfig(mergeConfig(config));

      if (!link) {
        link = createLink({});
      }

      ctx.slots.inject("shell.overlay", function () {
        return ctx.slots.register(
          { name: "shell.overlay", id: "ble-light", order: 9998, label: "蓝牙灯" },
          function (props) {
            return h(Bridge, props);
          }
        );
      });

      ctx.slots.inject("settings.section", function () {
        return ctx.slots.register(
          { name: "settings.section", id: "ble-light", order: 61, label: "蓝牙灯" },
          function (props) {
            return h(SettingsPage, props);
          }
        );
      });

      /* 卸载插件时断开连接、清掉所有定时器（不属于 slots 的副作用自己收尾） */
      if (typeof ctx.effect === "function") {
        ctx.effect(function () {
          return function () {
            if (link) {
              link.dispose();
              link = null;
            }
          };
        });
      }
    }

    exports.apply = apply;
    exports.inject = ["slots"];
    /* 测试内部（scripts/test.cjs 用；正常运行时无副作用） */
    exports.__internals = {
      DEFAULT_CONFIG: DEFAULT_CONFIG,
      CH: CH,
      SERVICE_UUID: SERVICE_UUID,
      TIMER_LEN: TIMER_LEN,
      MODE_CODE: MODE_CODE,
      EFFECT_OPTIONS: EFFECT_OPTIONS,
      STATE_KEYS: STATE_KEYS,
      MIN_MARQUEE_MS: MIN_MARQUEE_MS,
      MAX_MARQUEE_MS: MAX_MARQUEE_MS,
      STORAGE_KEY: STORAGE_KEY,
      mergeConfig: mergeConfig,
      resolveMode: resolveMode,
      deriveMode: deriveMode,
      stateFromInteractionKind: stateFromInteractionKind,
      buildDesired: buildDesired,
      targetFromState: targetFromState,
      planWrites: planWrites,
      countdownSeconds: countdownSeconds,
      hexToRgb: hexToRgb,
      normalizeHex: normalizeHex,
      shortUuid: shortUuid,
      formatRemain: formatRemain,
      uint16le: uint16le,
      describeError: describeError,
      createLink: createLink,
      getLink: getLink,
      setLink: function (next) {
        link = next;
      },
      setConfig: setConfig,
      getConfig: getConfig,
      resetConfig: resetConfig,
      initConfig: initConfig
    };
    return module.exports;
  }
});
