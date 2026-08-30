#ifndef CONVERTER_ENGINE_HPP
#define CONVERTER_ENGINE_HPP

#include "image_encoder.hpp"
#include "jp2_decoder.hpp"
#include "thread_pool.hpp"
#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include <chrono>

struct ConvertOptions {
    std::string input_path;
    std::string output_path;
    ImageFormat format{ImageFormat::JPG};
    int quality{90};
    size_t thread_count{0}; // 0 = auto detect
    bool recursive{false};
};

struct ConvertStats {
    size_t total_files{0};
    size_t success_count{0};
    size_t fail_count{0};
    std::chrono::milliseconds total_duration{0};
};

using ProgressCallback = std::function<void(size_t completed, size_t total, const std::string& current_file, bool success, const std::string& error_msg)>;

class ConverterEngine {
public:
    ConverterEngine() = default;
    ~ConverterEngine() = default;

    // Convert a single file
    static bool convert_single(const std::string& src_path, 
                              const std::string& dst_path, 
                              ImageFormat format, 
                              int quality = 90);

    // Run batch conversion
    static ConvertStats convert_batch(const ConvertOptions& options, 
                                     ProgressCallback progress_cb = nullptr);

    // Scan directory for JP2 files
    static std::vector<std::filesystem::path> scan_files(const std::filesystem::path& root_path, bool recursive);
};

#endif // CONVERTER_ENGINE_HPP
