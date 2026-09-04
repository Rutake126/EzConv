# EzConv - 极简 JPEG 2000 (JP2) 与 DjVu ➜ PDF 批量离线转换工具

一款轻量、高性能、开箱即用、完全离线的 **JPEG 2000 (`.jp2`, `.j2k`) 转 JPG / PNG** 以及 **DjVu (`.djvu`, `.djv`) ➜ 高保真低体积 PDF** 桌面图形客户端与 CLI 命令行工具。

---

## 核心特性

- **参考 DjVu.js Viewer 的设计风格**：清爽、无干扰的居中托盘虚线框设计，支持拖拽单个/多个文件或整个书籍文件夹，支持多语言切换与深浅色模式。
- **全新支持 DjVu ➜ PDF 工业级高保真低膨胀转换**：
  - **杜绝体积恶性膨胀**：基于 PDF 原生 MRC (Mixed Raster Content) 分层架构，文字图层采用高压缩无损二值编码，背景图层采用低采样紧凑压缩，彻底终结传统工具体积膨胀 10x~20x 的痛点！
  - **100% 刀锋级锐利文字**：文字层严格保持原生 1-bit 二值分辨率（300~600 DPI），在阅读器中放大 400%~1000% 依然绝无模糊毛刺与 JPEG 蚊状噪点。
  - **完整继承全文检索 (Searchable PDF)**：自动解析 DjVu `TXTz` 坐标与文本，无缝注入 PDF 标准隐形文字层（`3 Tr`），支持在 Acrobat/Edge/Chrome 中直接进行 `Ctrl+F` 搜词、划线高亮和复制。
  - **完整继承多级目录书签 (TOC)**：提取 DjVu `NAVI` 导航大纲，自动生成原生 PDF 书签树。
  - **三大转换模式**：智能分层模式 (Smart MRC, 推荐默认)、极速黑白无损模式 (Bitonal)、高清全彩模式 (Photo High-Q)。
- **真正的单文件绿色便携版**：前端 UI 资源直接内嵌编译在二进制可执行文件内，配合 `/MT` 静态运行时库与 OpenJPEG 静态编译，单个 `.exe` 放置在任意目录（如桌面、下载目录、U 盘）即可直接运行，零外部环境或 DLL 依赖。
- **高性能多线程并发**：内置 C++17 任务调度线程池，支持多核 CPU 并行流水线处理。
- **原生贴合的无边框窗口体验**：支持平滑拖拽移动与边缘缩放，最大化时智能适配 Windows 桌面工作区，**不会遮挡系统底部任务栏/导航栏**。
- **灵活配置**：支持 JPG 压缩质量调节 (1~100) 及 PNG 无损导出，支持自定义并发线程数与保存路径。
- **双模态支持**：同时提供现代化的 Windows GUI 桌面端 (`EzConv.exe`) 与高效的命令行端 (`jp2convert.exe`)。

---

## 更新日志 (Changelog)

### v1.1.0
- **新增 DjVu ➜ PDF 工业级转换支持**：
  - 实现基于 MRC 分层重建与无损二值掩码压缩算法，完美兼顾极致清晰度与超小文件体积；
  - 自动迁移 DjVu 内部的隐藏 OCR 文本层与多级书签目录树；
  - 桌面 GUI 前端自动识别 `.djvu` 并智能联动切换至 PDF 导出通道；
  - 命令行 CLI (`jp2convert.exe`) 全面支持 `.djvu` 转换与参数配置。

### v1.0.1
- **彻底实现单文件独立运行（Zero-Dependency Standalone）**：
  - 将前端 HTML/CSS/JS 静态资源整合并以内嵌字节流编译入 `EzConv.exe` 中，彻底解决因脱离 `app/` 文件夹导致启动抛出异常的问题。
  - 新增 `bundle_assets.py` 自动化打包脚本，并在 CMake 中集成自动化编译构建依赖。
- **优化窗口最大化体验（任务栏保护）**：
  - 处理 `WM_GETMINMAXINFO` 与 `WM_NCCALCSIZE` 消息，最大化窗口时精准吸附在系统工作区（Work Area），避免全屏覆盖遮挡底部的 Windows 任务栏/导航栏。
  - 支持窗口边缘自由拖动调整尺寸。
- **增强底层异常保护**：
  - 完善 Win32 消息循环及 COM 回调的异常拦截机制，提升软件在复杂环境下的运行稳定性。

---

## 目录结构

```text
├── app/                    # 桌面端前端界面源码 (HTML / CSS / JS)
├── docs/                   # 需求规格说明书 (DJVU_TO_PDF_PRD.md 等)
├── include/                # C++ 核心头文件 (解码器、线程池、DjVu-PDF引擎)
├── src/                    # C++ 源码实现 (GUI, CLI, 解码引擎, MRC合成器)
├── tests/                  # 测试样本与单元测试用例
├── third_party/            # 第三方依赖 (OpenJPEG, WebView2 SDK, DjVuLibre, stb 等)
├── bundle_assets.py        # 静态资源自动内嵌打包工具
├── CMakeLists.txt          # CMake 构建配置
└── README.md               # 项目说明文档
```

---

## 使用方法

### 1. 桌面图形客户端 (`EzConv.exe`)

1. 双击运行 `EzConv.exe`。
2. 将待转换的 `.jp2` / `.djvu` 文件或整个书籍文件夹**直接拖入虚线框**中（或点击手动选择）。
   - 若拖入的是 DjVu 文档，软件将自动智能切换为 **PDF 导出通道**。
3. 点击 **`- 查看所有选项`** 可自定义转换模式（智能分层 MRC / 纯黑白 / 全彩）、OCR 文字与书签保留开关、图像质量及并发线程数。
4. 点击 **`开始转换`**，页面将实时显示页码处理进度，转换完成后支持一键定位或保存。

### 2. 命令行工具 (`jp2convert.exe`)

```bash
# 1. 转换 DjVu 为高保真 PDF (默认启用 Smart MRC、保留 OCR 与目录书签)
jp2convert book.djvu -o book.pdf

# 2. 批量转换目录下的所有 DjVu 文件为 PDF
jp2convert -i ./scanned_books -o ./output_pdfs

# 3. 纯黑白古籍极速无损转换模式
jp2convert text_book.djvu -o text_book.pdf --djvu-mode bitonal

# 4. 转换 JP2 单个文件为 JPG / PNG
jp2convert image.jp2 -o output.jpg -q 95

# 5. 批量转换整个目录中的 JP2 文件
jp2convert -i ./scanned_book -o ./output_folder -f png -t 8
```

---

## 从源码构建

### 环境要求
- Windows 10 / 11
- Visual Studio 2019 / 2022 (包含 C++ 桌面开发组件)
- CMake 3.16+
- Python 3.x（可选，用于自动更新内嵌 UI）

### 编译步骤

```bash
# 1. 创建并进入构建目录
mkdir build
cd build

# 2. 生成 Visual Studio 解决方案 (CMake 会自动拉取 OpenJPEG 并打包内嵌 UI)
cmake ..

# 3. 编译 Release 版本
cmake --build . --config Release
```

编译生成的独立可执行文件位于 `build/_deps/openjpeg-build/bin/Release/EzConv.exe`。

---

##  开源许可

本项目遵循 MIT 开源许可证。
内部依赖的第三方库遵循各自对应的开源协议：
- [OpenJPEG](https://github.com/uclouvain/openjpeg) (BSD 2-Clause)
- [Microsoft WebView2](https://developer.microsoft.com/en-us/microsoft-edge/webview2/) (BSD 3-Clause)
- [CLI11](https://github.com/CLIUtils/CLI11) (BSD 3-Clause)
- [stb](https://github.com/nothings/stb) (MIT / Public Domain)
