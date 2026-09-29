# VoCat UI assets

界面运行时不读取外部 SVG。麦克风按钮已经在 `main/voice_ui.c` 中用 LVGL
圆角矩形、圆弧和线条绘制，按压与释放状态也由代码动画完成。

`main/clock_digits.bin` 是由 `tools/generate_clock_digits.ps1` 生成并嵌入固件的 4-bit
抗锯齿数字字模；保留在 `main` 下是为了兼容 ESP-IDF `EMBED_FILES` 的组件路径规则。
