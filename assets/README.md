<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

The workout application uses `fonts/workout_font_12.c`, `workout_font_16.c` and
`workout_font_20.c`: uncompressed 2 bpp subsets of Noto Sans CJK SC Regular,
licensed under the [SIL Open Font License](fonts/NotoSansCJKsc-OFL.txt). Source:
[Noto CJK](https://github.com/notofonts/noto-cjk/blob/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf).
The input OTF SHA-256 is
`2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`.
It is not necessary to compile or retain the full OTF in firmware.

Printable ASCII and the fixed Chinese inventory are in `fonts/workout_symbols.txt`.
`fonts/workout_font_inventory.h` drives runtime glyph checks; static tests check
each generated cmap. Compile all three sources through `main/CMakeLists.txt`.
Dynamic network names stay on the phone configuration page; this subset does not
claim arbitrary Chinese coverage.

Regenerate with the official `lv_font_conv` **1.5.3** CLI entry point:

```text
python3 tools/generate_workout_fonts.py --font <NotoSansCJKsc-Regular.otf> --converter <lv_font_conv.js>
```

The script collects UI characters, selects sizes 12/16/20, 2 bpp, no compression
and no kerning. It also generates `fonts/workout_digits_35.c`, an original 1 bpp
35 px pixel numeral font under this repository's MIT license. The maintained
5×7 digit patterns live in the script. Rerun the generator after adding Chinese
UI text; `tests/test_workout_fonts.py` detects an outdated inventory. Real-device
Chinese rendering and runtime network heap use remain separate checks.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.
