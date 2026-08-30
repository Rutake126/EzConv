# EzConv - 极简 JPEG 2000 (JP2) 批量离线转换工具

一款轻量、高性能、开箱即用、完全离线的 **JPEG 2000 (`.jp2`, `.j2k`) 转 JPG / PNG 桌面图形客户端与 CLI 命令行工具**。

---

## 🌟 核心特性

- **参考 DjVu.js Viewer 的设计风格**：清爽、无干扰的居中托盘虚线框设计，支持拖拽单个/多个文件或整个书籍文件夹，支持多语言切换与深浅色模式。
- **全离线与零依赖**：无需安装 Python、无需配置 `libvips` 环境变量，纯绿色单文件应用。
- **高性能多线程并发**：内置 C++17 任务调度线程池，基于 OpenJPEG 官方解码引擎，自动将 16-bit 灰度图平滑向下量化映射到 8-bit。
- **灵活配置**：支持 JPG 压缩质量调节 (1~100) 及 PNG 无损导出，支持自定义并发线程数。
- **双模态支持**：同时提供现代化的 Windows GUI 桌面端 (`EzConv.exe`) 与高效的命令行端 (`jp2convert.exe`)。

---

## 📁 目录结构

```text
├── app/                    # 桌面端前端界面资源 (HTML / CSS / JS)
├── include/                # C++ 核心头文件
├── src/                    # C++ 源码实现 (GUI, CLI, 解码引擎, 线程池)
├── third_party/            # 第三方依赖 (OpenJPEG 由 CMake 自动拉取, WebView2 SDK 等)
├── CMakeLists.txt          # CMake 构建配置
├── README.md               # 项目说明文档
└── REQUIREMENTS.md         # 详细技术设计规范与需求文档
```

---

## 🚀 使用方法

### 1. 桌面图形客户端 (`EzConv.exe`)

1. 双击运行 `EzConv.exe`。
2. 将待转换的 `.jp2` 文件或整个书籍文件夹**直接拖入虚线框**中（或点击手动选择）。
3. 点击 **`- 查看所有选项`** 可自定义输出格式（JPG/PNG）、压缩质量和并发线程数。
4. 点击 **`开始转换`**，转换后的图片将保存在源文件夹下的 `converted_images/` 目录中。

### 2. 命令行工具 (`jp2convert.exe`)

```bash
# 转换单个文件
jp2convert input.jp2 -o output.jpg -q 95

# 批量转换整个目录
jp2convert -i ./scanned_book -o ./output_folder -f png -t 8
```

---

## 🛠️ 从源码构建

### 环境要求
- Windows 10 / 11
- Visual Studio 2019 / 2022 (包含 C++ 桌面开发组件)
- CMake 3.16+

### 编译步骤

```bash
# 1. 创建并进入构建目录
mkdir build
cd build

# 2. 生成 Visual Studio 解决方案 (CMake 会自动下载并静态编译 OpenJPEG)
cmake ..

# 3. 编译 Release 版本
cmake --build . --config Release
```

编译生成的可执行文件位于 `build/bin/Release/` 或 `build/_deps/openjpeg-build/bin/Release/` 下。

---

## 📄 开源许可

本项目遵循 MIT 开源许可证。
内部依赖的第三方库遵循各自对应的开源协议：
- [OpenJPEG](https://github.com/uclouvain/openjpeg) (BSD 2-Clause)
- [Microsoft WebView2](https://developer.microsoft.com/en-us/microsoft-edge/webview2/) (BSD 3-Clause)
- [CLI11](https://github.com/CLIUtils/CLI11) (BSD 3-Clause)
- [stb](https://github.com/nothings/stb) (MIT / Public Domain)
