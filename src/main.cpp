#include "converter_engine.hpp"
#include "CLI11.hpp"
#include <iostream>
#include <iomanip>
#include <string>
#include <filesystem>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

static void print_banner() {
    std::cout << "\033[1;34m"
              << "=======================================================\n"
              << "   Google Material Design - JP2 批量极速转换工具 (C++)  \n"
              << "   Offline JP2 -> JPG/PNG High Performance Converter   \n"
              << "=======================================================\033[0m\n\n";
}

static void print_progress(size_t completed, size_t total, const std::string& current_file, bool success, const std::string& error_msg) {
    float percent = (total > 0) ? (static_cast<float>(completed) / total) * 100.0f : 100.0f;
    int bar_width = 30;
    int pos = static_cast<int>(bar_width * (percent / 100.0f));

    std::cout << "\r\033[K[";
    for (int i = 0; i < bar_width; ++i) {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << std::fixed << std::setprecision(1) << percent << "% "
              << "(" << completed << "/" << total << ") "
              << (success ? "\033[32m✔\033[0m " : "\033[31m✘\033[0m ")
              << current_file << std::flush;

    if (!success && !error_msg.empty()) {
        std::cout << " [" << error_msg << "]";
    }
}

int main(int argc, char** argv) {
    // Enable ANSI escape code support on Windows terminal
    #if defined(_WIN32)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
    #endif

    print_banner();

    CLI::App app{"High Performance Offline JP2 & DjVu Converter (JP2 -> JPG/PNG, DjVu -> PDF MRC)"};

    std::string input_path;
    std::string output_path;
    std::string format_str = "auto";
    std::string djvu_mode_str = "mrc";
    int quality = 90;
    size_t threads = 0;
    bool recursive = false;
    bool no_ocr = false;
    bool no_bookmarks = false;

    app.add_option("-i,--input", input_path, "Source JP2/DjVu file or directory path");
    app.add_option("-o,--output", output_path, "Output path or directory");
    app.add_option("-f,--format", format_str, "Target format: jpg, png, or pdf (default: auto)");
    app.add_option("-q,--quality", quality, "Quality factor (1-100, default: 90)");
    app.add_option("-t,--threads", threads, "Number of concurrent worker threads (0 = auto)");
    app.add_option("--djvu-mode", djvu_mode_str, "DjVu mode: mrc (Smart MRC), bitonal, or photo");
    app.add_flag("--no-ocr", no_ocr, "Disable OCR invisible text layer extraction");
    app.add_flag("--no-bookmarks", no_bookmarks, "Disable outline bookmarks extraction");
    app.add_flag("-r,--recursive", recursive, "Recursively scan subdirectories");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    // Interactive fallback mode if no input is supplied
    if (input_path.empty()) {
        std::cout << "\033[1;36m[交互模式]\033[0m 请输入或直接拖拽待转换的文件/文件夹路径 (.jp2, .djvu):\n> ";
        std::string raw_input;
        std::getline(std::cin, raw_input);

        // Strip surrounding quotes if dragged in on Windows
        if (raw_input.size() >= 2 && (raw_input.front() == '"' || raw_input.front() == '\'')) {
            raw_input = raw_input.substr(1, raw_input.size() - 2);
        }

        if (raw_input.empty()) {
            std::cout << "未输入有效路径，程序退出。\n";
            return 1;
        }
        input_path = raw_input;

        fs::path p(input_path);
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (ext == ".djvu" || ext == ".djv") {
            format_str = "pdf";
        } else {
            std::cout << "请选择输出格式 [1] JPG (默认)  [2] PNG  [3] PDF:\n> ";
            std::string choice;
            std::getline(std::cin, choice);
            if (choice == "2" || choice == "png" || choice == "PNG") {
                format_str = "png";
            } else if (choice == "3" || choice == "pdf" || choice == "PDF") {
                format_str = "pdf";
            } else {
                format_str = "jpg";
            }
        }
    }

    ConvertOptions opts;
    opts.input_path = input_path;
    opts.output_path = output_path;
    opts.quality = quality;
    opts.thread_count = threads;
    opts.recursive = recursive;

    std::string lower_fmt = format_str;
    std::transform(lower_fmt.begin(), lower_fmt.end(), lower_fmt.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (lower_fmt == "png") {
        opts.format = ImageFormat::PNG;
    } else if (lower_fmt == "pdf") {
        opts.format = ImageFormat::PDF;
    } else {
        opts.format = ImageFormat::JPG;
    }

    // DjVu 专用选项
    opts.djvu_options.bg_quality = quality;
    opts.djvu_options.keep_ocr = !no_ocr;
    opts.djvu_options.keep_bookmarks = !no_bookmarks;
    if (djvu_mode_str == "bitonal") {
        opts.djvu_options.mode = DjVuConvertMode::Bitonal;
    } else if (djvu_mode_str == "photo") {
        opts.djvu_options.mode = DjVuConvertMode::PhotoHighQ;
    } else {
        opts.djvu_options.mode = DjVuConvertMode::SmartMRC;
    }

    std::cout << "\n\033[1;33m[开始处理]\033[0m 扫描输入源: " << input_path << "\n";
    std::cout << "目标格式: " << ((opts.format == ImageFormat::JPG) ? "JPEG" : ((opts.format == ImageFormat::PNG) ? "PNG" : "PDF (MRC高保真)")) << "\n";

    ConvertStats stats = ConverterEngine::convert_batch(opts, [](size_t done, size_t total, const std::string& name, bool ok, const std::string& err) {
        print_progress(done, total, name, ok, err);
    });

    std::cout << "\n\n\033[1;32m================ 转换完成 ================\033[0m\n";
    std::cout << "  总文件数:   " << stats.total_files << "\n";
    std::cout << "  成功转换:   \033[32m" << stats.success_count << "\033[0m\n";
    std::cout << "  失败文件:   \033[31m" << stats.fail_count << "\033[0m\n";
    std::cout << "  总耗时:     " << (stats.total_duration.count() / 1000.0) << " 秒\n";
    std::cout << "\033[1;32m==========================================\033[0m\n";

    if (argc <= 1) {
        std::cout << "\n按回车键退出程序...";
        std::cin.get();
    }

    return 0;
}
