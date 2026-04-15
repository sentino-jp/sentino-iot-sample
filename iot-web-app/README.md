# Sentino BLE Tool

基于 Web Bluetooth API 的 Sentino IoT 设备配网调试工具。用于 BLE 连接设备、WiFi 配网、设备绑定和属性控制。

## 启动

```bash
cd iot-web-app
node server.js
# 打开 Chrome 访问 http://localhost:3000
```

> 需要 Chrome 浏览器（Web Bluetooth API 仅 Chrome 支持）。

## 项目结构

```
iot-web-app/
├── server.js          # HTTP 服务 + API 代理 (localhost:3000 → api-iot.sentino.jp)
├── index.html         # 五屏 UI
├── style.css          # 样式
├── js/
│   ├── app.js         # 主控逻辑：屏幕切换、事件绑定、配网流程
│   ├── api.js         # REST API 客户端 (登录、资产树、绑定状态查询)
│   ├── ble.js         # Web Bluetooth：扫描、GATT 连接、V1 消息收发
│   └── packet.js      # Rlink BLE V1 分包协议编解码
├── scan_uuid.py       # Python BLE 扫描工具 (bleak)
└── 交互流程图.md       # 完整五屏交互时序图
```

## 五屏交互流程

| 屏幕 | 功能 | 协议 |
|------|------|------|
| 01 Login | 用户认证，获取 userId/assetId/mqttUrl | REST API |
| 02 Scan | BLE 扫描 (0xA101)，连接 GATT (0x1910)，获取设备信息 | BLE V1 |
| 03 Config | WiFi 配置，输入设备 UUID（条码） | — |
| 04 Provisioning | 发送配网数据 (thing.network.set)，轮询绑定状态 | BLE V1 + REST API |
| 05 Control | 属性读写，设备通知 | BLE V1 |

详细时序图见 [交互流程图.md](交互流程图.md)。

## 关键业务配置

以下配置在 `js/api.js` 中，从 Sentino IoT 平台「App 开发」页面获取：

| 配置项 | 值 | 来源 |
|--------|-----|------|
| `app_id` | `krfjnsim9vs7yd` | App 开发页面 |
| `channel_identifier` | `gk6853gq` | App 开发页面 |
| `package_name` | `jp.sentino.general` | App 开发页面 |
| `data_center_code` | `cn` | App 开发页面 |
| `client_id` (Base64) | `Y2V0dXMtaW90LWFwcDo...` | OAuth2 凭证（待沟通获取方式） |
| `encrypt_type` | `AES/ECB/PKCS5Padding` | 固定值 |

## 与设备固件的配合

| 设备端 | App 端 | 说明 |
|--------|--------|------|
| BLE 广播 Service UUID `0xA101` | `ble.js` 过滤 `0xA101` | 设备发现 |
| GATT Service `0x1910` | `ble.js` 连接 `0x1910` | 数据通道 |
| V1 分包协议 (128B/包, 118B 有效) | `packet.js` 编解码 | 消息传输 |
| `device.information.get` 响应 | Screen 02 展示设备信息 | PID/版本/MAC |
| `thing.network.set` 接收 | Screen 04 发送配网数据 | SSID/密码/userId/assetId/mqttUrl |
| 设备 UUID (条码) | Screen 03 手动输入 | `checkBindResult` 轮询用 |

## API 代理

`server.js` 将 `/api/*` 请求代理到 `https://api-iot.sentino.jp/*`，解决浏览器跨域限制。
