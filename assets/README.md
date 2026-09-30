# VoCat UI assets

`eaf/xiaohei_blink_360.eaf` 是 page0 的开机动画，通过 `main/CMakeLists.txt` 嵌入固件，
由 `main/eaf_ui.c` 使用 Espressif EAF 播放器循环播放；默认帧间隔为 50 ms。
更新该文件后需要重新编译并烧录固件。
构建时 `tools/prepare_eaf.py` 会校验尺寸、帧表和校验和，并过滤播放器不支持的 `_C`
转换器元数据记录；原始 EAF 文件保持不变，生成的播放资源存放在构建目录。

界面运行时不读取外部 SVG。麦克风按钮已经在 `main/voice_ui.c` 中用 LVGL
圆角矩形、圆弧和线条绘制，按压与释放状态也由代码动画完成。

`main/clock_digits.bin` 是由 `tools/generate_clock_digits.ps1` 生成并嵌入固件的 4-bit
抗锯齿数字字模；保留在 `main` 下是为了兼容 ESP-IDF `EMBED_FILES` 的组件路径规则。
