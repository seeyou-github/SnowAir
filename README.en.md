<p align="center">
  <picture>
    <source media="(prefers-color-scheme: light)" srcset="./images/banner.png" />
    <img src="./images/banner.png" alt="ScreenShut" />
  </picture>
</p>
<h1 align="center">
  <span>ScreenShut</span>
</h1>
<p align="center">
  <span align="center">ScreenShut is a lightweight Windows screenshot and annotation tool covering screenshots, scrolling capture, screen recording, OCR, annotation, and pinning, available as the Lite edition and a more complete professional edition.</span>
</p>
<h3 align="center">
  <a href="#-editions">Editions</a>
  <span> · </span>
  <a href="#-installation">Installation</a>
  <span> · </span>
  <a href="Native/ARCHITECTURE.md">Architecture</a>
  <span> · </span>
  <a href="https://github.com/CM-idea/SnowAir/releases">Release Notes</a>
</h3>
<p align="center">
  <a href="README.md">简体中文</a>
  <span> | </span>
  <span>English</span>
</p>

## 🔨 Features

From region capture, screen recording, and presentation canvas to pinning, scrolling capture, and text recognition, ScreenShut ships an entire toolkit for everyday screenshots:

|   |   |   |
| --- | --- | --- |
| <picture><source media="(prefers-color-scheme: light)" srcset="images/icon/region.svg" /><img src="images/icon/region.svg" alt="Region Capture" height="20" /></picture>&nbsp;&nbsp; Region Capture | <picture><source media="(prefers-color-scheme: light)" srcset="images/icon/record.svg" /><img src="images/icon/record.svg" alt="Screen Recording" height="20" /></picture>&nbsp;&nbsp; Screen Recording | <picture><source media="(prefers-color-scheme: light)" srcset="images/icon/canvas.svg" /><img src="images/icon/canvas.svg" alt="Presentation Canvas" height="20" /></picture>&nbsp;&nbsp; Presentation Canvas |
| <picture><source media="(prefers-color-scheme: light)" srcset="images/icon/pin.svg" /><img src="images/icon/pin.svg" alt="Pin" height="20" /></picture>&nbsp;&nbsp; Pin | <picture><source media="(prefers-color-scheme: light)" srcset="images/icon/mirror.svg" /><img src="images/icon/mirror.svg" alt="Screen Mirror" height="20" /></picture>&nbsp;&nbsp; Screen Mirror* | <picture><source media="(prefers-color-scheme: light)" srcset="images/icon/action.svg" /><img src="images/icon/action.svg" alt="Super Action" height="20" /></picture>&nbsp;&nbsp; Super Action* |
| <picture><source media="(prefers-color-scheme: light)" srcset="images/icon/long.svg" /><img src="images/icon/long.svg" alt="Scrolling Capture" height="20" /></picture>&nbsp;&nbsp; Scrolling Capture | <picture><source media="(prefers-color-scheme: light)" srcset="images/icon/ocr.svg" /><img src="images/icon/ocr.svg" alt="OCR" height="20" /></picture>&nbsp;&nbsp; OCR | <picture><source media="(prefers-color-scheme: light)" srcset="images/icon/translate.svg" /><img src="images/icon/translate.svg" alt="Translate" height="20" /></picture>&nbsp;&nbsp; Translate |
| <picture><source media="(prefers-color-scheme: light)" srcset="images/icon/rect.svg" /><img src="images/icon/rect.svg" alt="Rectangle" height="20" /></picture>&nbsp;&nbsp; Rectangle | <picture><source media="(prefers-color-scheme: light)" srcset="images/icon/arrow.svg" /><img src="images/icon/arrow.svg" alt="Arrow" height="20" /></picture>&nbsp;&nbsp; Arrow | <picture><source media="(prefers-color-scheme: light)" srcset="images/icon/pen.svg" /><img src="images/icon/pen.svg" alt="Pen" height="20" /></picture>&nbsp;&nbsp; Pen |
| <picture><source media="(prefers-color-scheme: light)" srcset="images/icon/text.svg" /><img src="images/icon/text.svg" alt="Text" height="20" /></picture>&nbsp;&nbsp; Text | <picture><source media="(prefers-color-scheme: light)" srcset="images/icon/number.svg" /><img src="images/icon/number.svg" alt="Number" height="20" /></picture>&nbsp;&nbsp; Number | <picture><source media="(prefers-color-scheme: light)" srcset="images/icon/mosaic.svg" /><img src="images/icon/mosaic.svg" alt="Mosaic" height="20" /></picture>&nbsp;&nbsp; Mosaic |
| <picture><source media="(prefers-color-scheme: light)" srcset="images/icon/patina.svg" /><img src="images/icon/patina.svg" alt="Patina" height="20" /></picture>&nbsp;&nbsp; Patina | <picture><source media="(prefers-color-scheme: light)" srcset="images/icon/watermark.svg" /><img src="images/icon/watermark.svg" alt="Watermark" height="20" /></picture>&nbsp;&nbsp; Watermark | <picture><source media="(prefers-color-scheme: light)" srcset="images/icon/highlight.svg" /><img src="images/icon/highlight.svg" alt="Highlight" height="20" /></picture>&nbsp;&nbsp; Highlight |

Plus personalized extras such as a resident tray, global hotkeys, screenshot history, AI large models*, quick translate*, custom themes, and more.

> \* Starred items are professional edition features.

## 🖼️ Interface

Light and dark themes, switchable automatically with the system or set manually.

| Light Theme | Dark Theme |
| :---: | :---: |
| ![Light Theme](images/Bright.png) | ![Dark Theme](images/dark.png) |

## 🧩 Editions

ScreenShut comes in two editions that share the same experience and feature layout:

| E&#8288;d&#8288;i&#8288;t&#8288;i&#8288;o&#8288;n | Description | H&#8288;o&#8288;w&#8288; t&#8288;o&#8288; G&#8288;e&#8288;t |
| --- | --- | --- |
| **L&#8288;i&#8288;t&#8288;e** | A 4 MB-class ultra-small, single-file portable build covering everyday features like screenshots, annotation, scrolling capture, screen recording, OCR, and pinning. | [G&#8288;e&#8288;t&#8288; i&#8288;t][releases-link] |
| **P&#8288;r&#8288;o&#8288;f&#8288;e&#8288;s&#8288;s&#8288;i&#8288;o&#8288;n&#8288;a&#8288;l** | Built on Qt6, it offers a more complete set of professional features on top of the Lite edition (AI large models, screen mirror, super action, quick translate, etc.) along with ongoing update support | [G&#8288;e&#8288;t&#8288; i&#8288;t][releases-link] |

## 📦 Installation

The following covers how to obtain, run and extend the software; see [`Native/README.md`](Native/README.md) for details:
<br/><br/>
<details open>
<summary><strong>Software Download</strong></summary>
<br/>

Go to [ScreenShut Releases](https://github.com/CM-idea/SnowAir/releases), expand **Assets**, download `ScreenShut.exe`, and double-click to run — no installation required.

All data lives under `AppData\` next to the executable: `AppData\config\` holds configuration, language packs, plugins and machine-local state (`local.json`), screenshots and recordings default to `AppData\save\`, and runtime temporary files go to `AppData\temp\`. Copying the whole folder moves your settings with it; the screenshot/record directories can still be changed in Settings.

</details>

<details>
<summary><strong>Open-Source Scope</strong></summary>
<br/>

ScreenShut itself is open source under the **Apache License 2.0**. It depends on a customized Ling branch with unpublished implementation, which is not distributed here, so **building from this repository alone is not supported**.

To run ScreenShut, download `ScreenShut.exe` from [ScreenShut Releases](https://github.com/CM-idea/SnowAir/releases).

</details>

<details>
<summary><strong>Offline Components</strong></summary>
<br/>
Includes offline OCR, offline translation and other components, downloaded on demand and not bundled with the installer: you can download the required components and language packs under "Plugin Integrations", then enable them in "Feature Settings" to recognize Chinese, English and UI screenshots offline — no internet connection needed, and the engine is released as soon as it is done, so it never holds memory long-term.

</details>

<details>
<summary><strong>Command Line</strong></summary>
<br/>
When launched without arguments, it **only sits in the system tray on standby** and does not automatically enter capture mode:

```text
ScreenShut.exe                     # Tray standby, default
ScreenShut.exe --auto-quit=true    # Quit right after capturing
ScreenShut.exe --enter=pin         # Pin
ScreenShut.exe --enter=long        # Scrolling capture
ScreenShut.exe --enter=video       # Screen recording
ScreenShut.exe --enter=ocr         # Text recognition
ScreenShut.exe --enter=demo        # Presentation canvas
ScreenShut.exe --enter=edit        # Image editor
ScreenShut.exe --enter=tray        # Tray only, same as default behavior
```

Capture entry points: click the tray icon, and choose capture / open settings / do nothing under "Feature Settings → Tray Single Click"; or use the global hotkey `F1`; running the exe again opens the settings center.

</details>

## ✨ What's New

To learn about the latest changes, see the [Release Notes](https://github.com/CM-idea/SnowAir/releases).

## 🛣️ Roadmap

Keep refining annotation and screen-recording capabilities, improve OCR / translation service integration, and advance the settings center and toolbar appearance editor.

## ❤️ ScreenShut Community

ScreenShut is maintained by [Liuyiran](https://github.com/CM-idea). Feel free to report issues or share suggestions via [Issues](https://github.com/CM-idea/SnowAir/issues).

## 🔒 Privacy Statement

ScreenShut is a local tool and does not upload any screenshots or text by default; network access is used only for checking updates, online OCR / translation services, and plugin downloads.

## 🙏 Acknowledgments

Thanks to [xland](https://github.com/xland) and [ScreenCapture](https://github.com/xland/ScreenCapture) for their open source and sharing.

[![LINUX DO](https://img.shields.io/badge/LINUX%20DO-Community-1677FF?style=flat-square)](https://linux.do/)

## 📄 License

This project is open-sourced under the **Apache License 2.0**; see [`LICENSE`](LICENSE) for the full text, and [`NOTICE`](NOTICE) for third-party components and upstream attributions.

The open-source scope of this repository covers ScreenShut itself; the customized Ling it depends on is not part of it.

[releases-link]: https://github.com/CM-idea/SnowAir/releases
[issues-link]: https://github.com/CM-idea/SnowAir/issues
