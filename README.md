# VoCat Clock / Mic / Mouse

基于 ESP-IDF 6.1 和 LVGL 的 VoCat v1.0 固件。工程包含翻页时钟、USB 麦克风和
IMU 空鼠三个页面，支持 `ESP32-S3-WROOM-2-N32R16V` 与
`ESP32-S3-WROOM-1-N16R8` 两种硬件配置。

## 功能概览

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
VoCat Clock  <->  VoCat Mic  <->  VoCat Mouse
```

## 支持的硬件配置

| 型号 | Flash | PSRAM | Flash 模式 | 配置文件 | 分区表 |
| --- | ---: | ---: | --- | --- | --- |
| N32R16（默认） | 32 MB | 16 MB | Octal OPI/DTR | `sdkconfig.defaults.n32r16` | `partitions_n32r16.csv` |
| N16R8 | 16 MB | 8 MB | Quad QIO/STR | `sdkconfig.defaults.n16r8` | `partitions_n16r8.csv` |

两套配置均使用 6 MiB factory 应用分区，其余 Flash 用作 SPIFFS。普通的
`idf.cmd build` 和 `cfg` 使用工程根目录中的默认 N32R16 配置；`fmu` 会在构建前
读取设备 Flash 容量，在 N16R8 与 N32R16 配置之间自动选择。

## 环境要求

- Windows 10/11
- ESP-IDF 6.1，默认安装路径：`D:\esp-idf\v6.1\esp-idf`
- ESP-IDF Python 环境，默认位于 `C:\Espressif\tools`
- VoCat v1.0，通过 ESP32-S3 原生 USB 接口连接

工程附带的 `idf.cmd`、`cfg.cmd` 和 `fmu.cmd` 会自动加载 ESP-IDF PowerShell
环境，因此通常不需要手动执行 `export.ps1`。

## 快速开始

### 1. 配置 Wi-Fi、时区和 NTP

在工程目录执行：

```powershell
.\cfg.cmd
```

进入 `VoCat Desktop Clock` 菜单，设置：

- `Wi-Fi SSID`
- `Wi-Fi password`
- `POSIX timezone`，中国标准时间默认是 `CST-8`
- `NTP server`，默认是 `ntp.aliyun.com`
- `Use 24-hour time`

如果已经把 `F:\esp-proj\vocat` 加入用户 `PATH`，可以在任意目录直接执行：

```powershell
cfg
```

修改用户 `PATH` 后，需要重新打开 PowerShell 或 VS Code 终端。

### 2. 自动识别型号、编译、烧录和监视

```powershell
.\fmu.cmd
```

也可以指定串口或硬件型号：

```powershell
.\fmu.cmd COM6
.\fmu.cmd COM6 N16R8
.\fmu.cmd COM6 N32R16
```

设备必须停留在 Clock 页面，USB Serial/JTAG 串口才会存在。如果设备正处于
Mic/Mouse 页面，请先滑回 Clock 页面，再执行烧录命令。

### 3. 手动构建指定型号

```powershell
# N16R8
.\idf.cmd -B build-n16r8 "-DSDKCONFIG=sdkconfig.n16r8" "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults.n16r8" build

# N32R16
.\idf.cmd -B build-n32r16 "-DSDKCONFIG=sdkconfig.n32r16" "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults.n32r16" build
```

默认 N32R16 构建也可以使用：

```powershell
.\idf.cmd build
```

## 命令行工具

| 命令 | 作用 |
| --- | --- |
| `cfg` / `.\cfg.cmd` | 打开本工程的 ESP-IDF `menuconfig` |
| `fmu` / `.\fmu.cmd` | 自动识别串口和硬件、编译、烧录并打开可重连监视器 |
| `.\idf.cmd <参数>` | 在正确的 ESP-IDF 6.1 环境中执行任意 `idf.py` 操作 |

`cfg.cmd` 固定使用 VoCat 工程目录，因此加入 `PATH` 后可从任意目录调用。额外参数
会继续传给 `idf.py menuconfig`。

## USB 页面切换与日志

ESP32-S3 内部 USB PHY 由 USB Serial/JTAG 和 USB-OTG 共用：

- Clock 页面：UAC/HID 断开，USB PHY 用于 Serial/JTAG 日志和烧录。
- Mic/Mouse 页面：USB PHY 切换为 USB-OTG，Windows 枚举 `VoCat Mic` UAC 与
  键鼠 HID 复合设备，此时原 COM 端口暂时消失。
- 返回 Clock 页面：释放所有 HID 按键，断开 UAC/HID，恢复原 COM 端口。

VS Code ESP-IDF 自带 monitor 会把这种预期断线显示为红色 `ClearCommError`。
`fmu` 改用工程自带的 `vocat-monitor.py`：断线时安静等待，COM 端口恢复后自动
重连，不输出乱码或异常堆栈。按 `Ctrl+]` 退出监视器。

监视器默认按日志等级着色：

- Info：绿色
- Warning：黄色
- Error：红色
- Debug：蓝色
- Verbose：灰色
- 监视器状态：青色

单独启动或关闭颜色：

```powershell
python .\vocat-monitor.py COM6
python .\vocat-monitor.py COM6 --no-color
```

## 项目结构

```text
main/
  hello_vocat.c          应用入口、Wi-Fi、SNTP 与 Clock 心跳日志
  app_ui.c               Clock/Mic/Mouse 页面切换与 USB 生命周期
  clock_ui.c             固定双数字槽的 LVGL 翻页时钟
  voice_ui.c             麦克风按钮、回车触控区、光晕与波形
  mouse_ui.c             鼠标图案、左右键、滚轮与回中触控区
  air_mouse.c            BMI270 校准、滤波、零偏跟踪与位移计算
  voice_usb.c            ES7210 采集、UAC、键鼠 HID 与 USB PHY 切换
  usb_descriptors.c      UAC + 键鼠 HID 复合设备描述符
  vocat_v1_0.c           VoCat v1.0 LCD、I2S 和 ES7210 兼容层
  usb/tusb_config.h      TinyUSB UAC/HID 配置

sdkconfig.defaults       默认 N32R16 配置
sdkconfig.defaults.*     N16R8/N32R16 独立硬件配置
partitions*.csv          默认和两种硬件的分区表
cfg.cmd                  menuconfig 快捷命令
fmu.cmd / fmu-run.ps1    自动检测、构建、烧录和监视
idf.cmd / idf.ps1        ESP-IDF 6.1 命令包装器
vocat-monitor.py         彩色、可重连串口监视器
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
