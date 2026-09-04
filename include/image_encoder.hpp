#ifndef IMAGE_ENCODER_HPP
#define IMAGE_ENCODER_HPP

#include <string>
#include <vector>
#include <cstdint>

enum class ImageFormat {
    JPG,
    PNG,
    PDF
};

struct RawImage {
    int width{0};
    int height{0};
    int channels{0}; // 1 = Grayscale, 3 = RGB, 4 = RGBA
    std::vector<uint8_t> data;
};

class ImageEncoder {
public:
    // Write image to disk
    static bool encode_to_file(const std::string& output_path, 
                              const RawImage& image, 
                              ImageFormat format, 
                              int quality = 90);

    // Save as JPEG
    static bool save_as_jpeg(const std::string& output_path, 
                            int width, int height, int channels, 
                            const uint8_t* data, 
                            int quality = 90);

    // Save as PNG
    static bool save_as_png(const std::string& output_path, 
                           int width, int height, int channels, 
                           const uint8_t* data);
};

#endif // IMAGE_ENCODER_HPP
