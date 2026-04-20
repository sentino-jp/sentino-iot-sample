# BK7258 固件编译与烧录指南 (macOS)

本文档记录在 macOS (Apple Silicon) 环境下编译和烧录 BK7258 固件的完整流程。

## 环境要求

- macOS (Apple Silicon / Intel)
- Docker Desktop
- Python 3.8+
- Git

## 一、获取 bk_aidk 框架

```bash
cd ~/local
git clone --recurse-submodules https://github.com/bekencorp/bk_aidk.git -b ai_release/v2.0.1
cd bk_aidk
git checkout ai_release/v2.0.1.8
git submodule update --recursive
```

> 需要先从 BK 官方获得仓库访问权限。

## 二、替换 projects 目录

将本项目的 `device/projects` 目录复制到 bk_aidk 工程中：

```bash
rm -rf ~/local/bk_aidk/projects
cp -r ~/local/Conversational-AI-IOT-Sample/device/projects ~/local/bk_aidk/projects
```

## 三、配置

### 3.1 Agora ConvoAI 模式（默认）

编辑 `projects/common_components/network_transfer/agora_rtc/agora_config.h`，设置服务器 URL：

```c
#define CONFIG_AGENT_SERVER_URL         "http://your-server-ip:5001"
```

### 3.2 Sentino IoT 模式

在 `projects/beken_genie/config/bk7258/config` 中添加：

```
CONFIG_SENTINO_IOT=y
CONFIG_ALI_MQTT=y
```

启用后固件将使用 MQTT 信令（替代 HTTP），通过 Sentino IoT 平台获取 Agora RTC 参数。

#### 平台身份体系

```
Sentino IoT 平台
│
├── 应用 (App) ─── REST API 身份，仅 App 端使用，固件不需要
│   ├── OAuth2 clientId    cetus-iot-app                    ← HTTP Basic Auth
│   ├── OAuth2 clientSecret olADJCmWlFIVXY1q1lx0wUrUbze7XweR
│   ├── client_id header   Base64(clientId:clientSecret)    ← 同时用于业务请求头
│   ├── app_id             krfjnsim9vs7yd                   ← 业务层应用 ID
│   ├── channel_identifier gk6853gq
│   └── package_name       com.yiyuan
│
├── 产品 (Product) ─── 设备型号，同型号设备共享
│   ├── PID                vqB8C7fniWRLWL
│   ├── MQTT Topic 前缀    rlink/v2/{pid}/{uuid}/...
│   ├── 物模型定义
│   └── OTA 升级通道
│
├── 设备 (Device) ─── 三元组，每台设备唯一
│   ├── UUID               ct01kQBXBK7h63H8          ← MQTT 认证身份
│   ├── KEY                0c6ceda19a574413...        ← HMAC-SHA256 签名密钥
│   └── MAC                C8:47:8C:4F:20:FF
│
└── 用户 (User) ─── App 登录获得，通过 BLE 配网传给设备
    ├── userId             cn2042488223219761152
    └── assetId            2042488223647580161
```

- **App 端**用 `app_id` + `client_id` 调 REST API（用户登录、设备绑定）
- **设备端**用 `UUID` + `KEY` 连 MQTT broker，`PID` 用于 Topic 路径
- **配网绑定**：App 把 `userId` + `assetId` 通过 BLE 传给设备 → 设备 MQTT 上报 `bind` → 云端关联

> `app_id` 和 `PID` 是不同的标识：`app_id` 标识客户端应用，`PID` 标识产品型号。固件不需要知道 `app_id`。

#### 固件关键配置

| 配置项 | 值 | 文件 |
|--------|-----|------|
| Device UUID | `ct01kQBXBK7h63H8` | `sentino_mqtt.h` (硬编码，后续改为 NVS) |
| Device KEY | `0c6ceda19a574413...` | `sentino_mqtt.h` (硬编码，后续改为 NVS) |
| Product ID (PID) | `vqB8C7fniWRLWL` | `agora_rtc_main.c` (硬编码) |
| MQTT Broker | `mqtt-iot.sentino.jp` | BLE 配网传入，存 NVS |
| MQTT Port | `1883` | `sentino_mqtt.c` |
| MQTT Client ID | `rlink_{uuid}_V2` | `sentino_mqtt.c` |
| MQTT Username | `{uuid}\|signMethod=hmacSha256,ts={ts}` | `sentino_mqtt.c` |
| MQTT Password | `HMAC-SHA256("uuid={uuid},ts={ts}", key)` | `sentino_mqtt.c` |
| BLE ADV UUID | `0xA101` | `wifi_boarding_utils.c` |
| BLE GATT Service | `0x1910` | `wifi_boarding_utils.c` |

#### Sentino 集成踩坑记录

1. **MQTT Username 格式** — 不是简单的 `uuid`，必须是 `{uuid}|signMethod=hmacSha256,ts={timestamp}`，否则 broker 返回 `CONNACK not_authorized`
2. **ali_mqtt 需要设备信息初始化** — 在 `IOT_MQTT_Construct()` 之前必须调用 `iotx_device_info_init()` + `iotx_device_info_set()`，否则随机种子生成失败（`pdev = nil`）
3. **RTC uid 不能硬编码** — 云端返回的 `rtcToken` 绑定了特定的 `uid`（如 25532），设备必须用这个 uid 加入频道，否则 Agora 返回 `Invalid token (CCGA code=9)`
4. **datastream_queue 必须初始化** — Agora RTC 收到 Agent 的 stream message 时会往 `datastream_queue` 推送，如果队列为 NULL 会触发 `xQueueGenericSend` assert 导致设备重启
5. **BLE 广播数据 31 字节限制** — Legacy ADV 广播包和扫描应答包各限 31 字节（Bluetooth Core Spec Vol 6, Part B, §2.3.1），超出会返回 `bk_ble_gap_set_adv_data_raw err -5638`。需要将广播数据（Flags + Service UUID + Service Data）和厂商数据（Manufacturer Data）分别放在 ADV 包和 Scan Response 包中
6. **MQTT 协议版本** — ali_mqtt 使用 MQTT 3.1.1（protocol level 4），Sentino broker 实测可以接受（虽然文档标注 MQTT 5.0），但偶尔连接不稳定时重启设备即可

**信令对比**：

| | Agora ConvoAI（默认） | Sentino IoT |
|---|---|---|
| 信令协议 | HTTP POST | MQTT 5.0 |
| BLE 配网 | 0xfa00 / 多特征值 | 0x1910 / V1 分包 JSON |
| BLE 广播 UUID | 0xFE01 | 0xA101 |
| 获取 RTC 参数 | HTTP /device | MQTT agora_agent_device_access |
| 启动 Agent | HTTP /agent/start | 云端自动创建 |
| 停止 Agent | HTTP /agent/stop | 离开 RTC 频道即可 |
| OTA | HTTP 轮询 | MQTT issue 推送 |

> Agora RTC 音频通道在两种模式下完全相同。

## 四、使用 Docker 编译固件

macOS 无法直接编译，因为 BK 的打包工具（`cmake_Gen_image`、`cmake_encrypt_crc`）是 Linux x86-64 二进制文件。需要使用 Docker 容器完成编译。

### 4.1 创建 Dockerfile

在 `~/local/bk_aidk/` 目录下创建 `Dockerfile`：

```dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git python3 python3-pip \
    wget xz-utils ca-certificates && \
    rm -rf /var/lib/apt/lists/*

# 安装 ARM GCC 交叉编译工具链
RUN wget -q https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2 -O /tmp/gcc-arm.tar.bz2 && \
    tar xjf /tmp/gcc-arm.tar.bz2 -C /opt/ && \
    rm /tmp/gcc-arm.tar.bz2

# 安装 Python 依赖
RUN pip3 install --no-cache-dir click setuptools==69.5.1 future kconfiglib \
    click-option-group pycryptodome pyyaml cryptography

WORKDIR /bk_aidk
```

### 4.2 构建 Docker 镜像

```bash
cd ~/local/bk_aidk
docker build --platform linux/amd64 -t bk7258-build .
```

### 4.3 编译固件

```bash
docker run --platform linux/amd64 --rm \
  -v "$HOME/local/bk_aidk:/bk_aidk" \
  bk7258-build \
  make bk7258 PROJECT=beken_genie
```

### 4.4 编译产物

| 文件 | 路径 | 用途 |
|------|------|------|
| all-app.bin | `build/beken_genie/bk7258/all-app.bin` | 烧录固件 (~4.4M) |
| app_pack.rbl | `build/beken_genie/bk7258/encrypt/app_pack.rbl` | OTA 升级文件 (~1.8M) |
| app.elf | `build/beken_genie/bk7258/app.elf` | ELF 可执行文件 (调试用) |

### 4.5 编译其他项目

替换 `PROJECT` 参数即可编译不同项目：

```bash
# 可用项目列表
# beken_genie       - 声网 RTC 方案 (默认)
# beken_wss         - WebSocket 方案
# beken_genie_ab    - AB 分区 OTA 方案
# soundhub          - SoundHub 方案
# soundhub_wss      - SoundHub WebSocket 方案
# volc_rtc          - 火山引擎 RTC 方案
# lingxin           - 灵芯方案
# ai_dashboard      - AI 仪表盘

docker run --platform linux/amd64 --rm \
  -v "$HOME/local/bk_aidk:/bk_aidk" \
  bk7258-build \
  make bk7258 PROJECT=beken_wss
```

### 4.6 清理编译

```bash
rm -rf ~/local/bk_aidk/build
```

## 五、烧录固件

### 方案一：tyutool (推荐，跨平台)

[tyutool](https://github.com/tuya/tyutool) 是涂鸦开源的跨平台串口烧录工具，支持 macOS/Linux/Windows。

#### 安装

```bash
cd ~/local
git clone https://github.com/tuya/tyutool.git
cd tyutool
. ./export.sh
```

#### 烧录

```bash
cd ~/local/tyutool
.venv/bin/python tyutool_cli.py write \
  -d t5ai \
  -b 460800 \
  -p /dev/cu.usbserial-10 \
  -f ~/local/bk_aidk/build/beken_genie/bk7258/all-app.bin
```

> BK7258 对应 tyutool 的设备类型为 `t5ai`，波特率使用 `460800`（921600 会握手失败）。
> 串口设备路径请根据实际情况修改，可通过 `ls /dev/cu.usb*` 查看。

#### 烧录步骤

1. 用 Type-C 数据线连接开发板 `USB TO UART` 接口到电脑
2. **先按住 RST 按钮不放**
3. 执行上述烧录命令
4. 看到终端输出 `[INFO]: Waiting Reset ...` 后**松开 RST 按钮**
5. 等待擦除和写入完成，看到 `[INFO]: Flash write success.` 即烧录成功

> **注意**：烧录前确保没有其他程序占用串口（如 Chrome Web Serial）。
> 可通过 `lsof /dev/cu.usbserial-10` 检查。

### 方案二：bk7231tools (开源备选)

```bash
pip3 install bk7231tools

bk7231tools write_flash \
  -d /dev/cu.usbserial-10 \
  -b 1500000 \
  -s 0x0 \
  ~/local/bk_aidk/build/beken_genie/bk7258/all-app.bin
```

### 方案三：BKFIL (Windows 官方工具)

BK 官方烧录工具 BKFIL，仅支持 Windows。参考文档：
https://docs.bekencorp.com/arminodoc/bk_idk/bk7258/zh_CN/v2.0.1/developer-guide/config_tools/bk_tool_bkfil.html

命令行烧录示例：
```bash
bk_loader download -p 7 -i all-app.bin
```

## 六、使用说明

1. 开发板插入电池或数据线后自动启动
2. 长按 `S1` 5 秒进入配网模式，使用配套 Android APP 配网
3. 短按 `S2` 唤醒设备，启动和 AI Agent 的通话
4. 再次短按 `S2` 退出通话
5. 静默 3 分钟后自动进入深度休眠，按 `RST` 键重启

## 附：macOS 本地编译 (仅供参考)

如果不使用 Docker，macOS 上可以完成代码编译，但最终固件打包会失败（打包工具为 Linux 二进制）。

### 安装依赖

```bash
# ARM 交叉编译工具链
wget https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-mac.tar.bz2 -O /tmp/gcc-arm.tar.bz2
sudo tar xjf /tmp/gcc-arm.tar.bz2 -C /opt/

# Python 依赖
pip3 install --break-system-packages click 'setuptools<70' future kconfiglib \
    click-option-group pycryptodome pyyaml cryptography
```

> 注意：macOS 本地编译只能生成 `app.elf`，无法生成可烧录的 `all-app.bin`。
