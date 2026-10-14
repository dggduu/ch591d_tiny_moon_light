/**
 * dsh-ble-light — DSH 服务端插件入口。
 *
 * 本插件的全部功能都运行在浏览器端（见 client.js，通过 package.json 的
 * `dsh.client` 声明被 web 前端加载）：Web Bluetooth 只在浏览器里存在，
 * 服务端进程既没有蓝牙栈也不该接触设备。
 *
 * 这里保留一个空的 apply 函数，用于满足 Cordis loader 对根插件入口的加载
 * 要求，同时避免服务端启动阶段导入浏览器代码。插件的配置（cordis.patch.yml
 * 里的 config）会原样传给浏览器端的 apply —— 客户端侧读取同一行配置。
 */
export function apply() {}
