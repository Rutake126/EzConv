#ifndef JP2_DECODER_HPP
#define JP2_DECODER_HPP

#include "image_encoder.hpp"
#include <string>
#include <memory>

class JP2Decoder {
public:
    JP2Decoder() = default;
    ~JP2Decoder() = default;

    // Decode JP2 file into RawImage (RGB/Grayscale/RGBA, 8-bit per channel)
    static bool decode_file(const std::string& filepath, RawImage& out_image);
};

#endif // JP2_DECODER_HPP
