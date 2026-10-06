# 来源与许可

H1 应用、平台适配、构建工具和原创测试程序使用 GNU GPL version 3 or (at your option) any later version。
Copyright (C) 2026 HelloClyde and contributors. 完整许可见 LICENSE。

- GAM4980/6502 核心取自 HelloClyde/BBK9588-gam4980，固定提交
  `73b884a056ca0595de1552e6e365138687fb25a1`，上游 GPLv3。
  `s6502.c` 和 `gam4980_core.h` 保持原样；帧调度新增 BRK 停止检查，兼容头改为 H1 所需类型。
- H1 平台框架、文件选择器和测试固件表模拟器参考 HelloClyde/BBKH1-GBA，GPL-3.0-or-later。
  freestanding libc 起源于同作者 BBK9588-gba 的 `57c2792ec374b394df24c0ee898bb8624cf95a85`；
  H1 适配版本沿用 BBKH1-GBA 的 GPL-3.0-or-later 声明，不改变独立 9588 仓库的许可。
- `sdk/`：HelloClyde/bbk-h1-bda-sdk（原作者 MrDefinition1999），固定
  `067fe072477861dfc8949d7b1a55279fb92d2548`，Apache-2.0；保留其 LICENSE 和 NOTICE。
- `gam4980/src/platform/menu_font.h`：Noto Sans CJK SC 的渲染字形子集，
  Copyright (c) 2014-2021 Adobe，SIL Open Font License 1.1。
  许可副本位于 `gam4980/assets/licenses/NotoSansCJK-OFL.txt`。
- GAM4980 应用图标由 imagegen 为本项目生成，提示词和来源说明保存在 assets。
  运行截图呈现本项目界面或 H1 桌面；其中原系统图标仍属其各自权利人，应用 GPL 不为其重新授权。
- GNU MIPS 工具链为构建依赖，不随应用分发；GCC 15.2.0 源码：
  https://ftp.gnu.org/gnu/gcc/gcc-15.2.0/ 。

## 运行 ROM 的许可缺项

`gam4980/runtime/8.BIN` 与 `E.BIN` 从上述 BBK9588-gam4980 固定提交中的
`应用/数据/游戏/gam4980/` 原样取得，用于 GAM4980 启动和字库。
上游提供这些文件，但没有针对它们给出独立许可、原权利人或再分发授权声明。
本项目明确记录这一缺项，不主张这些第三方 ROM 属于应用代码的 GPL 授权范围。

| 文件 | SHA-256 |
| --- | --- |
| 8.BIN | `1c8f0b75f478cc42b1cc4292ff6c3b022b11384f0b6fc1b9601873a9da656d6f` |
| E.BIN | `9d13aa4593d97b790afc37d73da8be985e7a3aa7f3dcfe6b91c798671067aa5e` |

未包含商业 `.gam` 游戏、个人存档、完整 H1 固件/NAND、原机应用转储或工具链二进制。
