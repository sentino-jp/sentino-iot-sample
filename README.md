# Sentino IoT + BK7258 Conversational AI 示例项目

本项目展示如何基于 Sentino IoT 平台，在 BK7258 芯片上实现设备配网、云端信令和 AI 语音对话的完整链路。

## 架构概览

```
BLE 配网 Web App ──BLE V1──▶ BK7258 设备 ──MQTT──▶ Sentino IoT 云端
                                  │                        │
                                  │◀── Agora RTC 参数 ─────┘
                                  │
                                  ▼
                          Agora SD-RTN™ (音频通道)
                                  │
                                  ▼
                            AI Agent (ConversationalAI)
```

- **设备端 (BK7258)**：集成声网 RTSA Lite SDK，通过 MQTT 与 Sentino IoT 平台通信获取 RTC 参数，建立 Agora 音频通道与 AI Agent 实时对话。
- **配网工具 (web-app)**：基于 Web Bluetooth API 的浏览器端工具，通过 BLE V1 协议完成 WiFi 配网和设备绑定。

## 目录结构

| 目录 | 说明 | 文档 |
|------|------|------|
| `device/` | BK7258 固件工程（含多种方案） | [device/README.md](device/README.md)、[device/BUILD_GUIDE.md](device/BUILD_GUIDE.md) |
| `web-app/` | BLE 配网 Web 应用 | [web-app/README.md](web-app/README.md) |

## 快速开始

1. 参照 [device/BUILD_GUIDE.md](device/BUILD_GUIDE.md) 编译并烧录 BK7258 固件（Sentino IoT 模式）。
2. 启动配网工具：
   ```bash
   cd web-app && node server.js
   ```
   在 Chrome 中打开 `http://localhost:3000`，完成 BLE 配网。
3. 配网成功后，短按开发板 `S2` 键启动与 AI Agent 的语音对话。

## 关于声网 (Agora)

本项目的实时音频通道由声网提供。声网媒体流加速 (RTSA) 依托 Agora SD-RTN™ (Software Defined Real-time Network)，为 Linux/RTOS 设备提供音视频码流在互联网实时传输的能力，具有极小的包体积和内存占用，适合 BK7258 等资源受限的 IoT 设备。

使用 Sentino IoT 平台接入时，Agora ConvoAI 服务由 Sentino 统一提供，开发者无需单独向声网申请开通。

## 致谢

本项目基于 [Shengwang-Community/Conversational-AI-IOT-Sample](https://github.com/Shengwang-Community/Conversational-AI-IOT-Sample) 修改而来，感谢声网社区提供的原始示例工程。

## 技术支持

欢迎所有人参与到项目的开发和改进中来。如果你有任何想法或建议，请随时提出 Issue。
