#ifndef DJVU_PDF_ENGINE_HPP
#define DJVU_PDF_ENGINE_HPP

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <memory>

enum class DjVuConvertMode {
    SmartMRC,    // 智能分层模式：二值文字层(CCITT G4无损高锐利) + 背景层(低采样高质量JPEG)
    Bitonal,     // 极速黑白无损模式：仅提取二值文字层，体积极限小，速度最快
    PhotoHighQ   // 全彩高保真模式：整页RGB平铺编码，适合极端艺术画册
};

struct DjVuConvertOptions {
    DjVuConvertMode mode{DjVuConvertMode::SmartMRC};
    int bg_quality{75};          // 背景层/照片质量 (1~100)
    int bg_subsample{2};        // 背景层下采样率 (1=原分辨率, 2=一半分辨率, 3=三分之一)
    bool keep_ocr{true};        // 保留可搜索、可复制的隐藏 OCR 文本层
    bool keep_bookmarks{true};  // 保留多级大纲目录书签
    int max_threads{4};         // 并发处理线程数
};

struct DjVuConvertProgress {
    int current_page{0};
    int total_pages{0};
    std::string phase;          // 如 "正在提取页面", "正在编码图层", "正在合成PDF"
    double percent{0.0};
    int64_t source_size_bytes{0};
    int64_t output_size_bytes{0};
};

using DjVuProgressCallback = std::function<void(const DjVuConvertProgress& progress)>;

class DjVuPdfEngine {
public:
    // 获取 DjVu 文件的总页数
    static int get_page_count(const std::string& djvu_path);

    // 核心转换入口：将 DjVu 高保真低膨胀地转为 PDF
    static bool convert_djvu_to_pdf(const std::string& djvu_path,
                                    const std::string& pdf_path,
                                    const DjVuConvertOptions& options,
                                    DjVuProgressCallback progress_cb = nullptr);

    // 检查并确保 djvulibre 依赖工具就绪
    static bool ensure_tools_ready();

    // 获取当前检测到的工具根目录
    static std::string get_tools_dir();
};

#endif // DJVU_PDF_ENGINE_HPP
