# ScreenShut

轻雪 — Windows 原生截图工具（C++20 / Ling / D2D）。

## 特性

- 区域截图、标注、长截图、录屏（GIF / MP4）、OCR、二维码识别
- 托盘常驻、全局热键（默认 `F1`）
- 单 exe、静态链接 CRT；OCR 运行时 DLL 与模型随压缩包一起分发，无需运行时下载

## 目录

| 路径 | 说明 |
|------|------|
| [`Src/`](Src/) | 主程序源码 |
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | **架构速查**（Ling 框架 / SettingUi 浮层 / WinSetting 布局 / 待修 BUG），改代码前先读 |
| `deps/Ling/` | 定制版 Ling GUI 框架（未随仓库发布） |

## 开发环境

如需阅读或修改源码，建议安装：

- Windows 10 1803+
- Visual Studio 2026（v145 工具集，含「使用 C++ 的桌面开发」）
- Windows 10 SDK

## 构建说明

ScreenShut 本体以 **Apache License 2.0** 开源；运行依赖定制版 Ling（含未公开实现），该分支不随仓库发布，因此不提供从源码直接构建。

需要运行请使用 [Releases](https://github.com/CM-idea/SnowAir/releases) 中的 `ScreenShut.exe`。

## 命令行

不带参数启动时**只挂托盘图标待命**，不会自动进入截图模式：

```text
ScreenShut.exe                     # 托盘待命（默认）
ScreenShut.exe --auto-quit=true          # 截完即退出
ScreenShut.exe --enter=pin               # 贴图
ScreenShut.exe --enter=long              # 长截图
ScreenShut.exe --enter=video             # 录屏
ScreenShut.exe --enter=ocr               # 文字识别
ScreenShut.exe --enter=demo              # 演示画布
ScreenShut.exe --enter=edit              # 图片编辑
ScreenShut.exe --enter=tray              # 仅托盘（等同于默认行为）
```

截图入口：托盘图标单击（按「功能设置 → 单击托盘执行」：截图 / 打开设置 / 无操作）、
全局热键 `F1`、或再次运行 exe（打开设置中心）。

所有数据固定放在 exe 同目录的 `AppData\config\`，不再支持 exe 旁的便携 `config.json`。

## 数据目录

默认：`AppData\config\`（配置、插件、本机数据）

用户设置在 `config.json`；本机数据（上次选区等）在 `local.json`。截图/录像默认输出到 `AppData\save\`，临时文件在 `AppData\temp\`。程序不保留截图/录像历史，也不记录剪贴板历史。

## OCR 运行时

`deps/lw.PPOCR.C` 内置 Pure-C OCR 运行时源码（v1.2.2），CI 用 CMake 构建 `lw_ppocr_c.dll`，
连同轻量档官方 ONNX 模型一起打包到 `AppData\config\plugin\` 下：

- `ocr-runtime\ppocr-v6\lw_ppocr_c.dll`
- `ocr-models\ppocr-v6-tiny\{det,cls,rec}.lwm`、`ppocr_keys.txt`（CI 由官方 ONNX 转换）

旧版可选的 [ImageReader.exe](https://github.com/xland/ImageReader/releases) 仍可放到 `AppData\config\plugin` 或 exe 同目录。

由 [CM-idea](https://github.com/CM-idea) 维护。
