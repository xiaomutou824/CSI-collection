# ESP32-S3 CSI Collection

这是一个 ESP-IDF 工程，用于在 ESP32-S3 上采集 Wi-Fi CSI 原始数据，并通过串口输出 CSV。配套 Python 工具可以保存、实时查看、诊断和离线可视化 CSI 数据。

## 目录

```text
.
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild
│   └── main.c
└── tools/
    ├── requirements.txt
    ├── save_serial_csi.py
    ├── live_csi_monitor.py
    ├── visualize_csi.py
    └── inspect_csi_settings.py
```

## 配置与烧录

```bash
cd "/home/xing/project/wifi-sensing/CSI collection"
source /home/xing/.espressif/v5.5.4/esp-idf/export.sh
idf.py set-target esp32s3
idf.py menuconfig
```

在 `CSI Collection Configuration` 中填写：

```text
Wi-Fi SSID
Wi-Fi password
Node ID
CSI capture mode
Only print CSI frames from the connected AP BSSID
Only print CSI frames addressed to this ESP32 station
Only print HT 802.11n CSI frames
Generate CSI traffic by pinging the gateway from the ESP32
Internal gateway ping rate in Hz
```

默认 CSI 模式是 `merged_stable`，适合动作/存在检测这类稳定采集。普通路由器环境不稳定时可以试
`router_compatible_lltf`；需要看更原始 HT-LTF 时用 `raw_htltf`；探索不同 LTF 字段和 CSI 长度时用
`research_full`。默认开启 AP BSSID 过滤，只保存来自当前连接 AP 的 CSI 帧，减少其它设备包混入。
同时默认只保留发给本机 STA MAC 的 HT 数据帧，过滤 AP 广播/组播/管理帧，避免 RSSI 曲线出现这类非
ping 回复造成的尖峰。
默认也会由 ESP32 自己以 50 Hz ping 网关来产生下行 CSI，因此通常不再需要电脑端持续 ping。

本工程默认用 Custom UART0，波特率 `921600`：

```text
CONFIG_ESP_CONSOLE_UART_CUSTOM=y
CONFIG_ESP_CONSOLE_UART_TX_GPIO=43
CONFIG_ESP_CONSOLE_UART_RX_GPIO=44
CONFIG_ESP_CONSOLE_UART_BAUDRATE=921600
```

编译烧录：

```bash
idf.py build
idf.py flash
idf.py -b 921600 monitor
```

如果看到开头少量 ROM 乱码，通常正常；进入 app 后日志应恢复正常。

## 产生 CSI 数据

ESP32-S3 只有收到 Wi-Fi 包才会触发 CSI 回调。默认固件会自动 ping 网关来产生 CSI。串口日志中看到
下面这种信息就说明内部 ping 已启动：

```text
Internal gateway ping started: target=192.168.3.1, rate=50 Hz
CSI stats: fps=49.8, rows=500, callbacks=...
```

如果在 menuconfig 中关闭了内部 ping，可以看到板子 IP 后，在另一个终端手动持续 ping：

```bash
ping -i 0.02 192.168.3.19
```

`-i 0.02` 约等于 50 Hz，适合人体动作采集。

## 保存数据

```bash
python3 -m pip install -r tools/requirements.txt
python3 tools/save_serial_csi.py --port /dev/ttyUSB0 --baud 921600 --label idle --node 1
```

常用标签：

```text
idle
walking
sitting_down
standing_up
fall
```

## 实时查看

```bash
python3 tools/live_csi_monitor.py \
  --port /dev/ttyUSB0 \
  --baud 921600 \
  --label demo \
  --node 1 \
  --window 120 \
  --update-every 10
```

如果没有图形界面：

```bash
python3 tools/live_csi_monitor.py --port /dev/ttyUSB0 --baud 921600 --label demo --node 1 --no-plot
```

## 诊断数据质量

```bash
python3 tools/inspect_csi_settings.py data/node1_idle_xxx.csv
```

重点看：

```text
Approx. CSI frame rate: 20-50 fps 或更高
CSI stats: 固件运行时打印的实际 CSI fps 和过滤/跳序统计
Source MACs: 大多数应为当前 AP BSSID
First word invalid: 记录是否需要在后处理时忽略前 4 个 CSI 字节
CSI length bytes: 128 bytes
I/Q pairs parsed: 64 pairs 占大多数
Wi-Fi primary channels: 1-13
cwb bandwidth field: 0
RSSI: -35 到 -65 dBm 较合适
```

## 离线可视化

```bash
python3 tools/visualize_csi.py data/node1_idle_xxx.csv --drop-zero-subcarriers
```

输出在 `figures/`：

```text
*_amplitude_heatmap.png
*_mean_amplitude.png
*_rssi.png
```

训练前建议过滤非 64 I/Q pairs 的坏帧，并关注 RSSI 是否有突变。
