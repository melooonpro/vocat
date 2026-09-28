# VoCat ESP-IDF Example

面向 **VoCat v1.0 / ESP32-S3-WROOM-2-N32R16V** 的通用示例固件工程，不依赖 ESP-Claw。

默认界面是 LVGL 翻页时钟。顶部区域左滑依次进入 `VoCat Mic` 和
`VoCat Mouse`，右滑返回上一页。

## 功能

- 复用官方 `espressif/esp_vocat` 显示与触摸驱动，并提供 VoCat v1.0 引脚兼容层。
- 三张翻页牌显示时、分、秒，支持同步翻页动画。
- GPIO15 接收 ES7210 数据，同时采集 MIC1/MIC2，自动选择有效声道并输出 48 kHz / 16-bit 单声道。
- 语音和鼠标界面枚举为 `VoCat Mic` UAC 麦克风与 USB HID 键鼠复合设备。
- 按住圆形麦克风按钮时持续发送左 `Ctrl + Win`，松开、触摸丢失或退出页面时立即释放。
- 按住按钮时显示覆盖整块圆屏、随实际采样音量变化的绿色光晕。
- 麦克风界面的横向分隔线跨越全屏，在线下任意位置双击均可发送回车键。
- 鼠标界面使用 BMI270 陀螺仪控制光标；左右两侧分别是鼠标左、右键，支持单击、双击、长按和移动拖拽。
- 空鼠进入页面时会要求连续静止校准，运行中也会在确认静止后自动跟踪陀螺仪零偏并清除小数位移，抑制光标漂移。
- 屏幕下方的左、中、右三个区域分别用于左键、滚轮和右键；中间滚轮区宽 80 px，两侧按键区各宽 140 px，按下左右键时屏幕不显示色块。
- 在中间区上下滑动可滚动页面，滚动时鼠标图案中央竖线变绿；静止长按底部 `RECENTER` 区域 1 秒可将主机光标移到屏幕中心。
- 麦克风图标、圆形按钮、按压动效和波形完全由 LVGL 代码绘制，不依赖 SVG。
- 不包含蓝牙传输功能。

## 按页面切换 USB

- 时钟界面：UAC/HID 与主机断开，内部 USB PHY 交还给 USB Serial/JTAG，可查看运行日志。
- 语音/鼠标界面：内部 USB PHY 切换到 USB-OTG，Windows 会重新枚举 `VoCat Mic` 和键鼠 HID。
- 返回时钟界面：先释放所有键盘/鼠标按键，再断开 UAC/HID 并恢复日志接口。

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

工程自带 `fmu.cmd`，在工程目录中可直接执行：

```powershell
.\cfg.cmd                # 打开 ESP-IDF menuconfig
.\fmu.cmd                # 自动识别串口和 N16R8/N32R16，编译、烧录并打开 monitor
.\fmu.cmd COM5           # 手动指定串口，自动识别型号
.\fmu.cmd COM5 N16R8     # 无法自动检测时，手动指定 16 MB Flash / 8 MB PSRAM
.\fmu.cmd COM5 N32R16    # 手动指定 32 MB Flash / 16 MB PSRAM
```

将工程目录加入用户 `PATH` 后，可在任意目录直接使用 `cfg` 打开本工程的
`menuconfig`，并可使用 `fmu` 或 `fmu COM5`。更新 `PATH` 后需新开终端；
按 `Ctrl+]` 退出串口监视器。

`fmu` 会先读取芯片的 Flash 容量，再在构建前选择硬件配置：16 MB 使用
`sdkconfig.defaults.n16r8` 和 `partitions_n16r8.csv`，32 MB 使用
`sdkconfig.defaults.n32r16` 和 `partitions_n32r16.csv`。两种型号分别使用
独立的 `build-n16r8` / `build-n32r16` 目录，不会混用构建缓存。普通的
`.\idf.cmd build` 仍以当前 N32R16 配置作为默认配置。

`fmu` 烧录后使用工程自带的可重连日志监视器。切换到 Mic/Mouse 时，
USB Serial/JTAG 会按设计暂时消失；监视器只显示一条暂停提示，不输出
`ClearCommError` 或乱码，并在回到时钟页后自动连接同一个串口。若直接使用
VS Code ESP-IDF 扩展自带的 monitor，扩展仍会把这次预期断线显示为红色错误。
Clock 页每 10 秒输出一条包含当前时间、Wi-Fi 和 NTP 状态的心跳日志，用于
确认重连后的日志通道仍在工作；Mic/Mouse 页不会持续刷日志。

也可以不连接设备，直接构建指定型号：

```powershell
.\idf.cmd -B build-n16r8 "-DSDKCONFIG=sdkconfig.n16r8" "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults.n16r8" build
.\idf.cmd -B build-n32r16 "-DSDKCONFIG=sdkconfig.n32r16" "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults.n32r16" build
```

Linux 常见设备名为 `/dev/ttyUSB0` 或 `/dev/ttyACM0`；Windows 使用 `COMx`。

## 项目结构

```text
assets/                     素材说明；运行时不需要 SVG
main/
  app_ui.c                  时钟/Mic/Mouse 三页切换与 USB 生命周期
  clock_ui.c                LVGL 翻页时钟
  voice_ui.c                圆形麦克风、回车触摸区、动效与实时波形
  mouse_ui.c                鼠标图标、左右键与宽滚轮触摸区
  air_mouse.c               BMI270 校准、滤波与空鼠位移计算
  voice_usb.c               双麦采集、UAC、键鼠 HID 和 USB PHY 切换
  vocat_v1_0.c              v1.0 LCD 复位、GPIO15 I2S 和 ES7210 初始化
  usb_descriptors.c         VoCat Mic UAC + 键鼠 HID 复合设备描述符
  usb/tusb_config.h         TinyUSB UAC/HID 配置
  hello_vocat.c             Wi-Fi、SNTP 与应用入口
```

## License

Apache License 2.0，详见 [LICENSE](LICENSE)。
