# GAM4980 图标

`gam4980-icon.png` 是 H1 风格的新应用图标，由内置 imagegen 生成。
采用青绿色玻璃圆角底座、银色学习机和绿色像素游戏屏幕，减少小尺寸下不清晰的细节。
PNG 保留透明背景，构建时由固定版本 H1 SDK 转换为四组原生 BDA 图标资源。

风格参考：H1 V1.41 桌面底栏的原生应用图标及BBKH1-GBA 的应用图标。
主体参考：BBK9588-gam4980 的旧学习机图标；最终图像为重新生成的素材。
完整生成提示词见 [icon-prompt.txt](icon-prompt.txt)。

`src/platform/menu_font.h` 为 Noto Sans CJK SC 的预生成字形子集（OFL-1.1）。
字体 SHA-256：`2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`。
字体源：https://github.com/notofonts/noto-cjk/blob/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf 。

在仓库根目录使用 `python tools/generate_menu_font.py --font <NotoSansCJKsc-Regular.otf>` 再生。
普通构建直接使用已保存的字形；再生工具为 `tools/generate_menu_font.py --help`，需要原字体与 Pillow。
