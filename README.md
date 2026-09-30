# VoCat EAF / Clock / Mic / Mouse

基于 ESP-IDF 6.1 和 LVGL 的 VoCat v1.0 固件。工程包含 EAF 动画、翻页时钟、USB 麦克风和
IMU 空鼠四个页面，支持 `ESP32-S3-WROOM-2-N32R16V` 与
`ESP32-S3-WROOM-1-N16R8` 两种硬件配置。

## 功能概览

### VoCat EAF（page0）

- 开机默认显示 `assets/eaf/xiaohei_blink_360.eaf`，以 50 ms 帧间隔循环播放。
- 动画资源直接嵌入固件，无需单独下载文件系统。
- 离开 page0 时暂停动画，返回时继续播放。
- page0 和时钟 page1 均保留 USB Serial/JTAG 下载与监视端口。

### VoCat Clock

- 三张翻页卡片分别显示时、分、秒，并带同步翻页动画。
- 每张卡片固定分成左右两个等宽数字槽，十位和个位独立居中；`11`、`51` 等
  不同宽度组合切换时不会横向跳动。
- 新数字的下半叶片从铰链处由压扁状态向下展开，并带有移动边缘线；独立的不透明
  遮挡层同步盖住旧数字，兼顾实体翻页感和正确的前后遮挡。
- 通过 Wi-Fi 和 SNTP 自动校时，默认 NTP 服务器为 `ntp.aliyun.com`，失败后
  尝试 `pool.ntp.org`。
- 支持 12/24 小时制、POSIX 时区配置和日期显示。
- Clock 页每 10 秒输出一次时间、Wi-Fi、NTP 状态心跳。

### VoCat Mic

- ES7210 同时采集 MIC1/MIC2，自动选择有效声道。
- 向主机提供 48 kHz、16-bit、单声道 USB UAC 麦克风。
- 按住圆形麦克风按钮时发送 `Ctrl + Win`，松开、触摸丢失或离开页面时释放。
- 麦克风光晕根据实际采样音量变化。
- 分隔线下方任意位置双击可发送回车键。

### VoCat Mouse

- 使用 BMI270 陀螺仪控制 USB HID 鼠标光标。
- 进入页面时静止校准，运行中自动跟踪零偏并抑制静止漂移。
- 左右触控区对应鼠标左键和右键，支持单击、双击、长按和拖动。
- 中间 80 px 区域用于滚轮，上下滑动时滚轮图案变绿。
- 长按底部 `RECENTER` 区域 1 秒，可将主机光标移动到屏幕中心。

页面通过屏幕上方区域左右滑动切换：

```text
page0 EAF  <->  page1 Clock  <->  page2 Mic  <->  page3 Mouse
```

左滑进入下一页，右滑返回上一页；page0 右滑和 page3 左滑不切换页面。

## 支持的硬件配置

| 型号 | Flash | PSRAM | Flash 模式 | 配置文件 | 分区表 |
| --- | ---: | ---: | --- | --- | --- |
| N32R16（默认） | 32 MB | 16 MB | Octal OPI/DTR | `sdkconfig.defaults.n32r16` | `partitions_n32r16.csv` |
| N16R8 | 16 MB | 8 MB | Quad QIO/STR | `sdkconfig.defaults.n16r8` | `partitions_n16r8.csv` |

两套配置均使用 6 MiB factory 应用分区，其余 Flash 用作 SPIFFS。普通的
`tools/idf.cmd build` 和 `cfg` 使用工程根目录中的默认 N32R16 配置；`fmu` 会在构建前
读取设备 Flash 容量，在 N16R8 与 N32R16 配置之间自动选择。

## 环境要求

- Windows 10/11
- ESP-IDF 6.1，默认安装路径：`D:\esp-idf\v6.1\esp-idf`
- ESP-IDF Python 环境，默认位于 `C:\Espressif\tools`
- VoCat v1.0，通过 ESP32-S3 原生 USB 接口连接

工程 `tools/` 目录附带的 `idf.cmd`、`cfg.cmd` 和 `fmu.cmd` 会自动加载 ESP-IDF PowerShell
环境，因此通常不需要手动执行 `export.ps1`。

## 快速开始

### 1. 配置 Wi-Fi、时区和 NTP

在工程目录执行：

```powershell
.\tools\cfg.cmd
```

进入 `VoCat Desktop` 菜单，设置：

- `Wi-Fi SSID`
- `Wi-Fi password`
- `POSIX timezone`，中国标准时间默认是 `CST-8`
- `NTP server`，默认是 `ntp.aliyun.com`
- `Use 24-hour time`

如果已经把 `F:\esp-proj\vocat\tools` 加入用户 `PATH`，可以在任意目录直接执行：

```powershell
cfg
```

修改用户 `PATH` 后，需要重新打开 PowerShell 或 VS Code 终端。

### 2. 自动识别型号、编译、烧录和监视

```powershell
.\tools\fmu.cmd
```

也可以指定串口或硬件型号：

```powershell
.\tools\fmu.cmd COM6
.\tools\fmu.cmd COM6 N16R8
.\tools\fmu.cmd COM6 N32R16
```

VoCat 的 USB 仍用于下载固件。日志现在通过 ESP-NOW 发往第二块接收板；首次使用前，
请先按 [`F:\esp-proj\esp-now-log-s3\README.md`](F:\esp-proj\esp-now-log-s3\README.md)
配置并启动接收板。

### 3. 手动构建指定型号

```powershell
# N16R8
.\tools\idf.cmd -B build-n16r8 "-DSDKCONFIG=sdkconfig.n16r8" "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults.n16r8" build

# N32R16
.\tools\idf.cmd -B build-n32r16 "-DSDKCONFIG=sdkconfig.n32r16" "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults.n32r16" build
```

默认 N32R16 构建也可以使用：

```powershell
.\tools\idf.cmd build
```

## 命令行工具

| 命令 | 作用 |
| --- | --- |
| `cfg` / `.\tools\cfg.cmd` | 打开本工程的 ESP-IDF `menuconfig` |
| `fmu` / `.\tools\fmu.cmd` | 自动识别硬件、编译和烧录；可选监视 ESP-NOW 接收板 |
| `idf` / `.\tools\idf.cmd <参数>` | 在正确的 ESP-IDF 6.1 环境中执行任意 `idf.py` 操作 |

`tools/cfg.cmd` 固定使用 VoCat 工程目录，因此把 `tools` 加入 `PATH` 后可从任意目录调用。额外参数
会继续传给 `idf.py menuconfig`。

## USB 页面切换与日志

ESP32-S3 内部 USB PHY 由 USB Serial/JTAG 和 USB-OTG 共用：

- EAF（page0）和 Clock（page1）页面：UAC/HID 断开，USB PHY 可用于下载固件；两页之间切换不改变 USB 模式。
- Mic/Mouse 页面：USB PHY 切换为 USB-OTG，Windows 枚举 `VoCat Mic` UAC 与
  键鼠 HID 复合设备，此时原 COM 端口暂时消失。
- 从 Mic/Mouse 返回 Clock 或 EAF 页面：释放所有 HID 按键，断开 UAC/HID，恢复原 COM 端口。

VoCat 的应用日志由 `esp_log_set_vprintf()` 捕获，并通过 ESP-NOW 发给接收板。VoCat
同时保留 USB Serial/JTAG 本地日志和异常输出，EAF 和 Clock 页面可直接监视；Mic/Mouse 页面切换 USB PHY 后，本地 COM 口会暂时消失。
S3 接收板通过 CH343 UART 串口把收到的日志显示到电脑。两块板必须连接同一个
2.4 GHz Wi-Fi 网络，S3 接收端工程位于 `F:\esp-proj\esp-now-log-s3`；
C3 原生 USB 接收端工程位于 `F:\esp-proj\esp-now-log-c3`。

如果同时连接 VoCat 和接收板，可以把接收板 COM 口作为第三个参数，让 `fmu` 烧录后打开
接收端监视器：

```powershell
fmu COM6 N32R16 COM7
```

这里 `COM6` 是 VoCat 下载端口，`COM7` 是接收板监视器端口。只传 VoCat 端口和型号时，
`fmu` 烧录后会退出，并提示如何单独打开接收板监视器。

## 项目结构

```text
main/
  hello_vocat.c          应用入口、Wi-Fi、SNTP 与 Clock 心跳日志
  app_ui.c               EAF/Clock/Mic/Mouse 页面切换与 USB 生命周期
  eaf_ui.c               EAF 首页动画加载、循环播放与暂停
  clock_ui.c             固定双数字槽的 LVGL 翻页时钟
  voice_ui.c             麦克风按钮、回车触控区、光晕与波形
  mouse_ui.c             鼠标图案、左右键、滚轮与回中触控区
  air_mouse.c            BMI270 校准、滤波、零偏跟踪与位移计算
  voice_usb.c            ES7210 采集、UAC、键鼠 HID 与 USB PHY 切换
  usb_descriptors.c      UAC + 键鼠 HID 复合设备描述符
  vocat_v1_0.c           VoCat v1.0 LCD、I2S 和 ES7210 兼容层
  espnow_log.c           捕获、排队并可靠转发 ESP-IDF 应用日志
  usb/tusb_config.h      TinyUSB UAC/HID 配置

sdkconfig.defaults       默认 N32R16 配置
sdkconfig.defaults.*     N16R8/N32R16 独立硬件配置
partitions*.csv          默认和两种硬件的分区表
tools/
  cfg.cmd                  menuconfig 快捷命令
  fmu.cmd / fmu-run.ps1    自动检测、构建、烧录和可选的接收板监视
  idf.cmd / idf-run.ps1    ESP-IDF 6.1 命令包装器
  vocat-monitor.py         本地 VoCat 串口可重连监视器
  generate_clock_digits.ps1 生成翻页时钟数字素材
```

## 已知行为

- 切入或离开 Mic/Mouse 页面时，Windows 会发生一次 USB 断开和重新枚举，这是
  共用内部 USB PHY 的正常行为。
- USB 音频工作时，同一个原生 USB 接口不能同时保持 Serial/JTAG COM 端口。
- 自动型号选择依据实测 Flash 容量：16 MB 对应 N16R8，32 MB 对应 N32R16。
- 本工程不包含蓝牙传输功能。

## License

Apache License 2.0，详见 [LICENSE](LICENSE)。
