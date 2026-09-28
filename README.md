# VoCat ESP-IDF Example

面向 **VoCat v1.0 / ESP32-S3-WROOM-2-N32R16V** 的通用示例固件工程，不依赖 ESP-Claw。

默认界面是 LVGL 翻页时钟。向左滑动进入语音输入界面，向右滑动返回时钟。

## 功能

- 复用官方 `espressif/esp_vocat` 显示与触摸驱动，并提供 VoCat v1.0 引脚兼容层。
- 三张翻页牌显示时、分、秒，支持同步翻页动画。
- GPIO15 接收 ES7210 数据，同时采集 MIC1/MIC2，自动选择有效声道并输出 48 kHz / 16-bit 单声道。
- 语音界面枚举为 `VoCat Mic` UAC 麦克风与 USB HID 键盘复合设备。
- 按住圆形麦克风按钮时持续发送左 `Ctrl + Win`，松开、触摸丢失或退出页面时立即释放。
- 按住按钮时显示覆盖整块圆屏、随实际采样音量变化的绿色光晕。
- 麦克风图标、圆形按钮、按压动效和波形完全由 LVGL 代码绘制，不依赖 SVG。
- 不包含蓝牙传输功能。

## 按页面切换 USB

- 时钟界面：UAC/HID 与主机断开，内部 USB PHY 交还给 USB Serial/JTAG，可查看运行日志。
- 语音界面：内部 USB PHY 切换到 USB-OTG，Windows 会重新枚举 `VoCat Mic` 和键盘 HID。
- 返回时钟界面：先释放 `Ctrl + Win`，再断开 UAC/HID 并恢复日志接口。

页面切换会触发一次 Windows USB 设备断开与重新连接，这是正常行为。

## Windows 环境

项目自带 ESP-IDF 6.1 PowerShell 包装脚本：

```powershell
.\idf.cmd menuconfig
.\idf.cmd build
.\idf.cmd -p COM5 flash
```

也可以手动加载环境：

```powershell
D:\esp-idf\v6.1\esp-idf\export.ps1
```

在 `menuconfig` 的 **VoCat Desktop Clock** 中设置 Wi-Fi、密码、时区和 NTP 服务器。

## 构建与烧录

```powershell
.\idf.cmd set-target esp32s3
.\idf.cmd build
.\idf.cmd -p COM5 flash
```

首次烧录后，在 Windows“设置 → 系统 → 声音 → 输入”中选择 `VoCat Mic`。只有进入语音界面时该设备才会出现。

## PowerShell 快捷命令（可选）

将以下内容加入 PowerShell Profile（`notepad $PROFILE`）：

```powershell
function bd { & .\idf.cmd build }
function fmu0 { & .\idf.cmd -p COM5 flash monitor }
function fmu {
    $port = Get-CimInstance Win32_SerialPort |
        Where-Object { $_.Name -match 'USB|JTAG|UART' } |
        Select-Object -First 1 -ExpandProperty DeviceID
    if (-not $port) { throw '没有找到可用串口' }
    & .\idf.cmd -p $port flash monitor
}
```

Linux 常见设备名为 `/dev/ttyUSB0` 或 `/dev/ttyACM0`；Windows 使用 `COMx`。

## 项目结构

```text
assets/                     素材说明；运行时不需要 SVG
main/
  app_ui.c                  双 App 页面、滑动切换与 USB 生命周期
  clock_ui.c                LVGL 翻页时钟
  voice_ui.c                圆形麦克风按钮、动效与实时波形
  voice_usb.c               双麦采集、UAC、HID 和 USB PHY 切换
  vocat_v1_0.c              v1.0 LCD 复位、GPIO15 I2S 和 ES7210 初始化
  usb_descriptors.c         VoCat Mic UAC + HID 复合设备描述符
  usb/tusb_config.h         TinyUSB UAC/HID 配置
  hello_vocat.c             Wi-Fi、SNTP 与应用入口
```

## License

Apache License 2.0，详见 [LICENSE](LICENSE)。
