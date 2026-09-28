# VoCat Flip Clock

适用于 **ESP-VoCat v1.1 / ESP32-S3-WROOM-2-N32R16V** 的独立桌面翻页时钟。

本项目直接使用 ESP-IDF 和轻量 BSP 驱动 360 × 360 ST77916 圆形屏幕，不依赖 ESP-Claw、LVGL 或其他 UI 框架。

## 功能

- 小时、分钟、秒钟三张机械翻页卡片
- 带重力加速感的连续翻页动画
- 多卡片同时翻动时保持统一帧率
- 4 bit 抗锯齿数字字库
- Wi-Fi STA 联网和 SNTP 自动校时
- NTP 超时后自动尝试 `ntp.aliyun.com`
- 可配置时区、NTP 服务器和 12/24 小时制
- 针对 32 MB Octal Flash + 16 MB Octal PSRAM 配置
- Windows 和 Linux 均可构建

## 硬件

| 项目 | 配置 |
| --- | --- |
| 开发板 | ESP-VoCat v1.1 |
| 模组 | ESP32-S3-WROOM-2-N32R16V |
| Flash | 32 MB Octal Flash |
| PSRAM | 16 MB Octal PSRAM |
| 屏幕 | 360 × 360 ST77916 QSPI LCD |
| ESP-IDF | 6.1 或更高版本 |

### 关于 v1.1 硬件版本

Espressif 公开资料目前主要覆盖 ESP-VoCat v1.0 和 v1.2，没有单独发布 v1.1 原理图。本项目的 BSP 使用两版公开设计中一致的显示接口，并兼容两种 LCD 复位线路：

- v1.0：GPIO3，低电平复位
- v1.2：GPIO47，高电平复位
- LCD/SD 电源：GPIO9，低电平使能
- LCD QSPI、背光和片选引脚位于 `components/esp_vocat_v1_1`

在没有确认 v1.1 原理图前，请勿直接启用可能占用 GPIO3 的 I2S 音频输入。

参考资料：

- [ESP-VoCat 硬件文档](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp-vocat/index.html)
- [Espressif ESP-VoCat BSP 1.1.0](https://components.espressif.com/components/espressif/esp_vocat/versions/1.1.0/readme)
- [官方显示驱动实现](https://github.com/espressif/esp-bsp/blob/master/bsp/esp_vocat/src/bsp_display.c)

## 获取项目

```bash
git clone https://github.com/melooonpro/vocat.git
cd vocat
```

首次构建时，ESP-IDF Component Manager 会自动下载官方 `esp_lcd_st77916` 组件。

## 配置 Wi-Fi 和时间

```bash
idf.py set-target esp32s3
idf.py menuconfig
```

进入 `VoCat Desktop Clock`，配置：

- `Wi-Fi SSID`
- `Wi-Fi password`
- `POSIX timezone`
- `NTP server`
- `Use 24-hour time`

中国大陆默认时区为：

```text
CST-8
```

POSIX 时区符号与常见 UTC 写法相反，因此北京时间使用 `CST-8`，不是 `UTC+8`。

> [!IMPORTANT]
> Wi-Fi 名称和密码会以明文写入本地 `sdkconfig`。该文件已加入 `.gitignore`，请勿手动提交或分享。

## 构建和烧录

### Windows PowerShell

从开始菜单打开 **ESP-IDF 6.1 PowerShell**，然后执行：

```powershell
cd C:\path\to\vocat
idf.py build
idf.py -p COM5 flash monitor
```

将 `COM5` 替换为设备管理器中实际显示的串口。使用 `Ctrl+]` 退出串口监视器。

如果普通 PowerShell 中无法识别 `idf.py`，说明尚未加载 ESP-IDF 环境。可以打开 ESP-IDF PowerShell，或者运行 ESP-IDF 安装目录中的 PowerShell profile。

仓库中的 `idf.cmd`/`idf.ps1` 是 Windows 环境辅助脚本。其 ESP-IDF 安装路径可能需要根据本机环境修改：

```powershell
.\idf.cmd menuconfig
.\idf.cmd build
.\idf.cmd -p COM5 flash monitor
```

### Linux

```bash
. "$HOME/esp/esp-idf/export.sh"
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

串口也可能显示为 `/dev/ttyUSB0`，请按实际设备修改。

### 常用 PowerShell 快捷函数

如果已经在 PowerShell profile 中配置了本项目的快捷函数，可以使用：

```powershell
bd       # 构建
fmu0     # 使用枚举到的第 0 个 USB 串口烧录并监视
fmu1     # 使用枚举到的第 1 个 USB 串口烧录并监视
fmu      # 依次轮询可用 USB 串口
```

这些是用户级 PowerShell 函数，并非 ESP-IDF 内置命令。

## 重新生成数字字库

Windows 上可以使用以下脚本重新生成 `main/clock_digits.bin`：

```powershell
.\generate_clock_digits.ps1
```

脚本使用 Windows 的 `System.Drawing` 和 Arial Bold 生成 4 bit 抗锯齿数字位图。修改字库尺寸后，需要同步检查 `main/clock_ui.c` 中的 `DIGIT_W` 与 `DIGIT_H`。

## 项目结构

```text
vocat/
├── components/esp_vocat_v1_1/   # VoCat v1.1 N32R16 最小 BSP
├── main/
│   ├── hello_vocat.c             # Wi-Fi、SNTP 和应用主循环
│   ├── clock_ui.c                # 翻页时钟渲染与动画
│   ├── clock_ui.h
│   ├── clock_digits.bin          # 4 bit 抗锯齿数字字库
│   └── Kconfig.projbuild         # menuconfig 项目配置
├── generate_clock_digits.ps1     # 数字字库生成脚本
├── sdkconfig.defaults            # ESP32-S3 N32R16 默认配置
├── idf.cmd / idf.ps1             # Windows 构建辅助脚本
└── CMakeLists.txt
```

## 常见问题

### `idf.py` 无法识别

需要先加载 ESP-IDF 环境。Windows 推荐直接使用开始菜单中的 ESP-IDF PowerShell。

### 找不到 `xtensa-esp32s3-elf-gcc`

重新运行 ESP-IDF Tools Installer，或在 ESP-IDF 目录中执行安装脚本，然后重新打开终端。

### OpenOCD 无法打开 FTDI 设备

普通烧录不需要 OpenOCD。请选择 UART 烧录，并使用 VoCat 的 USB Serial/JTAG 串口：

```powershell
idf.py -p COM5 flash monitor
```

### NTP 首次同步超时

SNTP 会继续在后台重试。项目还会自动尝试 `ntp.aliyun.com`，成功同步后屏幕会显示 `NTP SYNC`。

## 许可证

本项目采用 [Apache License 2.0](LICENSE)。第三方组件分别遵循其自身许可证。

