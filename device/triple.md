# Sentino 三元组 CLI 操作手册

## 前置

- 串口：115200 8N1
- 推荐工具：`tio /dev/cu.usbserial-XXXX`（退出 `Ctrl-T Q`）
- 设备需跑带 SENTINO_IOT 的固件（启动日志含 `sentino_:W ... triple loaded from flash`）

---

## `get_triple` — 查询当前三元组

**用法**：无参
```
$ get_triple
```

**有 triple 时输出**：
```
sentino_:W: uuid  =ct01VWfvv171KS7s
sentino_:W: secret=5b9b31365144429faa6f7aac3e76adf8
sentino_:W: pid   =OQm9yRoaLq1gbK
sentino_:W: mac   =444AD60EE392
```

**无 triple 时**：
```
sentino_:W: no triple loaded (state=0)
```
state=0 表示 `SENTINO_DEV_UNAUTHORIZED`，下游 MQTT/RTC 链路启动会被拒。

---

## `set_triple` — 写入新三元组到 NVS

**用法**：4 参数，空格分隔，**顺序固定**：
```
set_triple <uuid> <secret> <pid> <mac>
```

**示例**：
```
$ set_triple ct01VWfvv171KS7s 5b9b31365144429faa6f7aac3e76adf8 OQm9yRoaLq1gbK 444AD60EE392
sentino_:W: triple written: uuid=ct01VWfvv171KS7s pid=OQm9yRoaLq1gbK — reboot to take effect
```

**字段长度上限**：
| 字段   | 最大 | 备注                                |
| ------ | ---- | ----------------------------------- |
| uuid   | 63   | sentino 唯一设备 ID                 |
| secret | 63   | hex 字符串，HMAC-SHA256 password 用 |
| pid    | 31   | 产品型号 ID                         |
| mac    | 17   | 12 hex 或 `AA:BB:CC:DD:EE:FF` 格式  |

**生效时机**：写完**必须 reboot**，正在跑的 MQTT 不会热切换：
```
$ reboot
```

**报错**：参数数量不对会返回 `usage: set_triple <uuid> <secret> <pid> <mac>`，写 NVS 失败会返回 `write_to_flash failed: <ret>`。

---

## `reset_triple` — 擦除 NVS 里的三元组

**用法**：无参
```
$ reset_triple
sentino_:W: triple wiped
sentino_:W: reset_triple ret=0
```

**生效时机**：立即生效（state 变 `UNAUTHORIZED`），但**已经连上的 MQTT/RTC 不会主动断**——建议 reboot。

**reboot 后行为**：
- 如果固件**编译时定义了** `SENTINO_TRIPLE_TEST`（当前 dev build）：启动时检测 NVS 空 → 用 `sentino_dev_info.h` 里的 `SENTINO_TEST_*` 默认值重新写入。等于回到出厂默认 triple。
- 如果**未定义** `SENTINO_TRIPLE_TEST`（生产 build）：留在 `UNAUTHORIZED` 状态，等待厂测/动态注册下发 triple。

---

## 常见操作流程

### 临时换设备身份测试
```
get_triple                           # 看当前
set_triple ct01XYZ... abc... OQm9... 444AD60EFFFF
reboot
get_triple                           # 验证
```

### 回滚到编译期默认
```
reset_triple
reboot
get_triple                           # 输出 SENTINO_TEST_* 那套
```

### 完全空白（仅生产 build 或临时关 SENTINO_TRIPLE_TEST 时）
```
reset_triple
reboot
get_triple                           # 应输出 no triple loaded
```

---

## NVS 存储位置

- 后端：EasyFlash KV
- key：`d_stn_triple`
- 结构：`sentino_dev_triple_record_t`（magic + flag + 三元组 + 64B reserve）
- 不会被 `factory_reset` 按键擦掉（`key_app_service.c` 里 reset 流程只清 provision + WiFi，**保留 triple**）

## 故障速查

| 现象                                      | 可能原因                                                                     |
| ----------------------------------------- | ---------------------------------------------------------------------------- |
| `cmd NOT found: get_triple`               | 固件没带 SENTINO_IOT，或 `sentino_iot_init()` 没在启动时被调                 |
| `set_triple` 后 reboot triple 没变        | `write_to_flash failed`；检查 NVS 分区是否满 / EasyFlash 是否初始化          |
| 启动日志没看到 `triple loaded from flash` | sentino_dev_info_load 没被调；查 boarding_core / sentino_iot_engine 启动顺序 |
| MQTT 连不上但 triple 正确                 | 检查 secret 是否 32 字符 hex、broker 是否可达                                |