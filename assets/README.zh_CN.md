<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

运动应用使用 `fonts/workout_font_12.c`、`workout_font_16.c` 和
`workout_font_20.c`，来自 Noto Sans CJK SC Regular，为未压缩的 2 bpp 子集，
遵循 [SIL 开放字体许可证](fonts/NotoSansCJKsc-OFL.txt)。来源：
[Noto CJK](https://github.com/notofonts/noto-cjk/blob/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf)。
输入 OTF 的 SHA-256 为
`2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`。
固件不需要编译或保留完整 OTF。

可打印 ASCII 和固定中文字符集保存在 `fonts/workout_symbols.txt`。
`fonts/workout_font_inventory.h` 用于运行时字形检查，静态测试检查各字体生成的 cmap。
三个字体源文件通过 `main/CMakeLists.txt` 编译。任意网络名称仅在手机配置页显示，
这个子集不声明支持任意中文。

使用官方 `lv_font_conv` **1.5.3** 的 CLI 入口重新生成：

```text
python3 tools/generate_workout_fonts.py --font <NotoSansCJKsc-Regular.otf> --converter <lv_font_conv.js>
```

脚本收集界面字符，选择 12 / 16 / 20 px、2 bpp、不压缩、无字距调整。同时生成
`fonts/workout_digits_35.c`，它是按仓库 MIT 许可证提供的原创 1 bpp、35 px 像素
数字字体；5×7 数字图案在脚本中维护。新增中文文案后重新生成，
`tests/test_workout_fonts.py` 会检测过期字符集。真机中文显示和联网时的堆内存
仍需分别验证。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。
