# BK7258 AI Demo 开发板原理图分析

> 源文件：[bk7258_schematic/source.pdf](./bk7258_schematic/source.pdf)（来自 [Beken 官网](https://docs.bekencorp.com/HW/BK7258/AIDK_AI%E7%8E%A9%E5%85%B7%E5%BC%80%E5%8F%91%E6%9D%BF_%E5%8E%9F%E7%90%86%E5%9B%BE.pdf)）
> 转出图片：`bk7258_schematic/page-1.jpg` … `page-6.jpg`（200 DPI）
> 已与原 PDF 逐页核对，下文标 ⚠️ 处为原稿 page1 方框图与 page3-6 详图之间存在的不一致（详图为准）。

## 文档基本信息

- **标题**：BK7258 AI Demo Schematic V1.0
- **厂商**：Beken（博通集成）
- **日期**：2025-03-17（page 1/2/4/5/6）/ 2025-02-24（page 3）
- **形态**：AI 玩具开发板 / Demo 板原理图
- **规格**：A4 × 6 页，模块化视图
- **主控**：BK7258 QFN88 9×9

---

## Page 1 — 系统方框图

整板能力一览（方框图为概览，**具体型号以详图为准**）：

| 域 | 关键器件 / 接口 | 备注 |
|---|---|---|
| **电源** | Type-C×2 + 充电管理 ETA3422 ⚠️（OVP/OCP）→ 锂电 4.2V；ADC0 检测电池电压；LDO/DC-DC = ME6211C33M5G-N 给 3.3V | ⚠️ 详图 page 3 实际是 **ETA4322** |
| **USB-UART 串口** | CH340（UART0），单 Type-C 出 5V + USB DP/DM | 详图 page 3 是 **CH340E** |
| **MCU 外设** | UART1、IIC1/Power、SDIO、QSPI0/QSPI1、PWM、ADC、GPIO | IIC1 接 G-sensor & 电源；DVP 摄像头用 **IIC2** |
| **音频** | MIC×2（MICBIAS/MICP1/MICN1, MICP2/MICN2）+ Audio PA HT6872 ⚠️（4.2V，AUDLP/AUDLN 差分驱动） | ⚠️ 详图 page 5 实际是 **HT6873** |
| **显示** | LCD1 + LCD2 双 160×160 | 接 BK7258 的 QSPI 控制器引脚，但每屏只用 1 根数据线 = 标准 **SPI**（D/C+CS+SCL+SDA+RST），见 page 5 |
| **存储** | SD NAND 1GB（SDIO） | 容量只在方框图标，详图未标具体料号 |
| **图像** | DVP 摄像头 GC2145（24-pin DVP） | 板上 RS3236 可调 DVDD，兼容 GC2053/HM1055 |
| **传感** | 陀螺仪 SC7A20H（IIC1） | 实际 page 4 标 SC7A20HTR，VDDGPIO 供电支持睡眠唤醒 |
| **NFC** | MFRC522（13.56MHz 卡片，UART1 接） | 13.56MHz 是天线/载波，芯片晶振 **27.12MHz** = 载波×2 |
| **振动** | 马达驱动（PWM，P9） | MMBT3904 + 1N5819W 飞轮二极管 |
| **指示** | LED×2、3 颗按键、开关机/按键 | LED1=红 充电指示 / LED2=绿 充满指示 |
| **无线** | 板载 BLE/WiFi 2.4G 天线（IPEX 可选） | 模块 BWU.FL-IPEX1 + Winwave-3216F3 巴伦 |

---

## Page 2 — BK7258 主控核心

- **天线模块**：P1 = `BWU.FL-IPEX1`（板载天线 + IPEX 可选）+ U13 = `Winwave-3216F3` 巴伦
- **天线匹配网络**：C1=1pF / C2=1.2pF / L2=3.0nH / L1=0R / C5=0.4pF / C88=0R，注释 "匹配部分器件靠近芯片 ANT 脚"
- **晶振两颗**：
  - 主时钟 `26 MHz`（12pF, 85C），外配 C3/C4 = 10pF 负载电容
  - RTC `32.768 kHz`（X2），外配 C16/C17 = 18pF
  - 注释 "晶体负载电容 2016 封装 CL=7~9pF, 2016 封装外部匹配电容 NC, 3225 外部..."
- **接口排针**：烧录口、调试/扩展口（UART1_TX/RX 引出）、RESET 按键、螺丝孔
- **GPIO 复用全标在 die 周围**——改板找脚的主索引：
  - DVP：`P27/DVP_MCLK`, `P29/DVP_PCLK`, `P30/DVP_HSYNC`, `P31/DVP_VSYNC`, `P32~P39 = DVP_D0~D7`
  - UART1：`P20/UART1_RXD`, `P21/UART1_TXD`
  - SDIO：`P15/SD_CLK`, `P16/SD_CMD`, `P17~P19 = SD_D0~D2`, 等
  - QSPI0/QSPI1（LCD/FLASH）
  - 32K 晶振接 `P7/32K_XI` + `P8/32K_XO`
  - PWM、IIC、ADC、JTAG 等

---

## Page 3 — 电源域

- **USB**：USB1 + USB2 = 2 颗 `TYPE-C 16PIN 2MD(073)` 母座
  - CC1/CC2 接 R3/R15 = 5.1K 下拉（identify as UFP/sink）
  - DP/DM：R7/R8 = 0R 串联 + C81/C82 = 4.7pF + D3/D4 TVS；D5 = SMF5.0A（5V TVS over VBUS）
  - USB1 → `USB_DP/USB_DM` 走信号
  - USB2 → CH340E（U12）→ TX0/RX0，R109/R110 = 100R 串阻
- **充电（U2 = ETA4322）**：
  - 引脚：NC/IN/NTC/ISET/GND/EPAD/NC/VBAT/STBY/STAT/NCE/CHRG
  - R5 = 2R（电流采样）、R6 = 49.9K、R10 = 5.1K、C22 = 10uF（VBAT 滤波）
  - 注释 **"电池没有带 NTC 电阻 → R16 焊 0R；电池带 NTC 10K → R6 换 49.9K"**
  - 注释 **"FULL_DET 输入上拉；5V_DET 为高、FULL_DET 为低则充满"**
  - LED1 = `RED`（CHRG 指示）、LED2 = `GREEN`（满电指示）
  - CN2 = `CONN-SMD_3P-P1.25`（电池连接器：BAT+ / NTC / GND）
- **外部 LDO（U3 = ME6211C33M5G-N）**：VBAT → 3.3V (`LDO_3V3`)，`LDO33_EN` 控使能
- **Camera LDO（U4 = ME6211C28M5G-N，2.8V 变种）**：VBAT → +2.8V，`DVP_PWR_CTL` 控使能
- **Camera DVDD（U5 = RS3236-ADJ8YF5 可调 LDO）**：
  - 公式 `Vo = (Ru+Rd)/Rd × 0.81`
  - R28 选阻：`1.2V = 30K / 1.5V = 51K / 1.8V = 75K`
  - 注释 "不同 DVP 模块用不同 DVDD"（GC2053=1.2V / HM1055=1.5V / GC2145=1.8V）
- **5V 直供电路**：Q1 = `SI2301CDS-T1-GE3`（P-MOSFET 高边开关），R21=10K + R24=100K + LI_BAT 供给 → 5V

---

## Page 4 — DVP / G-Sensor / SD NAND / 马达 / 按键 LED

- **DVP 摄像头（H1 = 24-pin 排针）**：
  - 数据/同步：`D0~D7, MCLK, PCLK, VSYNC, HSYNC, RST, PWDNB`
  - I2C 控制走 **`IIC2_SDA/IIC2_SCL`**（10K 上拉 R36/R38）——**不是 IIC1**
  - 电源：`AVDD2.8V / IOVDD / DVDD`（DVDD 由 page 3 的 RS3236 给）
  - D9 = `ESD9B3.3ST5G-N` ESD 二极管
- **G-Sensor（U6 = SC7A20HTR）**：
  - **`IIC1_SDA/IIC1_SCL`**，4.7K 上拉（R42/R43/R44）
  - 地址通过 SDO 选 `0x18` / `0x19`，INT1/INT2 中断
  - 注释 **"VDDGPIO 供电，支持睡眠唤醒"**
- **SD NAND（U7）**：
  - SDIO 4-bit + CMD/CLK，3.3V 供电（R45 = 0R 选通）
  - `SD_D0/D1/D2/D3, SD_CLK, SD_CMD`
  - R52/55/56/57/58 = 10K 上拉，注释 **"CMD 和 DAT 必需加上拉电阻"**
  - 容量未在原理图标，page 1 方框图标 1GB
- **马达振子**：CN10 = `MOTO-10mm`，Q2 = `MMBT3904` 驱动，D8 = `1N5819W` 飞轮二极管，由 P9 通过 R51=1K 控制
- **按键 ×3**：S1/S2/S3，KEY1/2/3 各串 1K 电阻
- **LED ×2**：LED1 = `RED`（R63 = 1K）；LED2 = `GREEN`（R64 = 330R）
- **外扩焊点**：K1/K2/K3、R、G 引出

---

## Page 5 — LCD / 音频

- **两块 LCD**（LCD1 + LCD2，12-pin）：
  - 实际接口为标准 **SPI**：`D/C, CS, SCL, SDA, RESET`（每屏只走 1 根数据线）
  - 信号名以 `LCD_QSPI_*` / `FL_QSPI_*` 命名 = 接 BK7258 的 QSPI 控制器引脚，并不真正用 4 线 QSPI
  - LCD1：`LCD_QSPI_D1` → D/C, `LCD_QSPI_CS` → CS, `LCD_QSPI_CLK` → SCL, `LCD_QSPI_D0` → SDA, `LCD_RST` → RESET
  - LCD2：`LCD_QSPI_D3` → D/C, **`FL_QSPI_CS` / `FL_QSPI_CLK` / `FL_QSPI_D0`**（共用 Flash QSPI 总线！）, `LCD_QSPI_D2` → RESET
  - 注释 "**双屏**"，背光 LDO_3V3 + `LCD_BL` 共用
- **背光驱动**：`LCD_BL_PWM` → R61=1K → Q3 (MMBT3904) → `LCD_BL` 节点 → R59=10R
- **MIC ×2**（CN7 + CN9 = `HC-1.25-2PLT`）：
  - MICBIAS 偏置 + 1K 串阻 + 10pF/100pF/220nF 耦合滤波
  - **MIC 推荐型号：`AM_065200-GML020`**（GML020，不是 GMJ020）
- **喇叭（U8 = HT6873 Class-D PA）** ⚠️ 不是 page 1 标的 HT6872：
  - VBAT 供电（C83 = 22uF），差分输出 OUT+/OUT- → CN8 = `HC-1.25-2PLT` 喇叭口（C49 = 10uF 串联）
  - `AUDLP/AUDLN` 经 1uF + 20K（R74/R75）入 IN+/IN-
  - PA_SD 由 `MUTE` 经 R70=1K 控制，R71=10K 默认下拉
  - **AEC 回采路径**：PA_VDD / OUT 经 1uF + **39K**（R83/R84）回采到 AUDLN/AUDLP
  - 注释 "**AEC 需要增加回采电路，R83/R84 阻值根据输出音量做适当调整**"

---

## Page 6 — NFC（MFRC522）

- **U9 = MFRC522**，VCC_NFC 由 LDO_3V3 经 R85=0R 供电，R86=10K 上拉 NRST，27 个数字 IO（D1~D7, ALE, IRQ, RX, A0/A1 等）
- **X3 = 27.12 MHz 晶振**（12pF 负载 C62/C68）— 注意：page 1 标的 "13.56M" 是天线/卡片载波频率，MFRC522 内部 ÷2 得到 13.56MHz
- **天线匹配 / EMC 滤波 / 接收电路** 模板电路 + P2 IPEX 天线接口（B.MJ.FL-IPEX1）
- **P2 跳线选 UART vs I2C**：
  - Short to **Pin1** → output **I2C** protocol
  - Short to **Pin3** → output **UART** protocol
- **A1/A0 协议选择表**（依 R87/R88/R90/R91 = 1K 分压组合）：

  |    | UART | SPI | IIC |
  |----|------|-----|-----|
  | A1 |  0   |  0  |  1  |
  | A0 |  0   |  1  | EA  |

  （默认 **"默认 Uart 接口"** 标签 = UART）
- **默认 UART 接口信号映射**（NFC 芯片 → BK7258，均 0R 串阻可改）：
  - `TXD` →R94→ `UART1_RXD`
  - `RXD` →R95→ `UART1_TXD`
  - `MX` →R96→ `NFC_MX`
  - `DTRQ` →R98→ `NFC_DTRQ`
  - `IRQ` →R99→ `NFC_IRQ`

---

## 文档自身的型号不一致（page 1 方框图 vs page 3-5 详图）

| 模块 | Page 1 方框图标 | Page 3-5 详图标 | 以哪个为准 |
|---|---|---|---|
| 充电管理 | `ETA3422` | **`ETA4322`** | 详图（实际 BOM/Layout） |
| Audio PA | `HT6872` | **`HT6873`** | 详图 |
| NFC 晶振 | `13.56M`（实为载波） | `27.12 MHz`（X3） | 都对，是同一事实的两种表述 |

下单/替换时一律以详图料号为准。

---

## 对本工程的关联（结合 CLAUDE.md）

1. **充电检测语义**：CLAUDE.md 提到的 `charge_status` DP 应配 R 不可写——硬件依据就在 page 3：FULL_DET 是 ETA4322 STAT/CHRG 引脚硬连，是检测信号不是控制信号；且充满判定要 `5V_DET=H AND FULL_DET=L` 双信号。
2. **MIC 物料**：原理图推荐 `AM_065200-GML020`（GML020 不是 GMJ020）。AEC 回采阻 R83/R84 起始 **39K**，按实际音量调。
3. **NFC 默认 UART1 + 5 根 0R 串阻可改线**——固件如果想换 IIC，硬件先飞 P2 跳线 + R94/R95/R96/R98/R99，并把 R87/R88/R90/R91 重新分压让 A1=1, A0=EA。
4. **DVP 与 G-sensor 走不同 IIC**：DVP = IIC2，G-sensor = IIC1。配 sensor 驱动别搞混总线。
5. **GC2145 摄像头 DVDD=1.8V** = R28 选 75K；如果换 GC2053 要把 R28 换成 30K。
6. **LCD 真是 SPI 不是 QSPI**，信号命名误导；其中 LCD2 跟 SPI Flash 共享 `FL_QSPI_*` 总线 → 双屏 + 外挂 Flash 同时用时要注意时序冲突。
7. **5V 直供电路**走 P-MOSFET（Si2301）由 GPIO 控制——外设上电控制（外设电源控制信号）就走这里。
8. **RESET 按键**在 page 2 主控旁边；CLAUDE.md memory 里的 "USB 三联哑火 → 按 RST 恢复" 就是这颗。
