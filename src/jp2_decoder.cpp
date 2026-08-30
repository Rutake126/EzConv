#ifndef OPJ_STATIC
#define OPJ_STATIC
#endif
#include "jp2_decoder.hpp"
#include <openjpeg.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <filesystem>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace fs = std::filesystem;

static void error_callback(const char *msg, void *client_data) {
    (void)msg;
    (void)client_data;
}

static void warning_callback(const char *msg, void *client_data) {
    (void)msg;
    (void)client_data;
}

static void info_callback(const char *msg, void *client_data) {
    (void)msg;
    (void)client_data;
}

// Memory buffer stream callbacks for OpenJPEG
struct MemBufferStream {
    const uint8_t* pData;
    OPJ_SIZE_T nSize;
    OPJ_SIZE_T nOffset;
};

static OPJ_SIZE_T mem_read_fn(void* p_buffer, OPJ_SIZE_T p_nb_bytes, void* p_user_data) {
    auto* s = static_cast<MemBufferStream*>(p_user_data);
    if (!s || !s->pData || s->nOffset >= s->nSize) {
        return static_cast<OPJ_SIZE_T>(-1);
    }
    OPJ_SIZE_T bytes_left = s->nSize - s->nOffset;
    OPJ_SIZE_T to_read = (p_nb_bytes < bytes_left) ? p_nb_bytes : bytes_left;
    std::memcpy(p_buffer, s->pData + s->nOffset, to_read);
    s->nOffset += to_read;
    return to_read;
}

static OPJ_BOOL mem_seek_fn(OPJ_OFF_T p_nb_bytes, void* p_user_data) {
    auto* s = static_cast<MemBufferStream*>(p_user_data);
    if (!s || p_nb_bytes < 0 || static_cast<OPJ_SIZE_T>(p_nb_bytes) > s->nSize) {
        return OPJ_FALSE;
    }
    s->nOffset = static_cast<OPJ_SIZE_T>(p_nb_bytes);
    return OPJ_TRUE;
}

static OPJ_OFF_T mem_skip_fn(OPJ_OFF_T p_nb_bytes, void* p_user_data) {
    auto* s = static_cast<MemBufferStream*>(p_user_data);
    if (!s || p_nb_bytes < 0) {
        return -1;
    }
    OPJ_SIZE_T new_offset = s->nOffset + static_cast<OPJ_SIZE_T>(p_nb_bytes);
    if (new_offset > s->nSize) {
        new_offset = s->nSize;
    }
    s->nOffset = new_offset;
    return s->nOffset;
}

static void mem_free_user_data_fn(void* p_user_data) {
    (void)p_user_data;
}

// Decode using OpenJPEG
static bool decode_with_openjpeg(const uint8_t* data, size_t size, CODEC_FORMAT codec_format, RawImage& out_image) {
    opj_codec_t* codec = opj_create_decompress(codec_format);
    if (!codec) {
        return false;
    }

    opj_set_info_handler(codec, info_callback, nullptr);
    opj_set_warning_handler(codec, warning_callback, nullptr);
    opj_set_error_handler(codec, error_callback, nullptr);

    opj_dparameters_t parameters;
    opj_set_default_decoder_parameters(&parameters);

    if (!opj_setup_decoder(codec, &parameters)) {
        opj_destroy_codec(codec);
        return false;
    }

    MemBufferStream memStream{ data, static_cast<OPJ_SIZE_T>(size), 0 };
    opj_stream_t* stream = opj_stream_create(size, OPJ_TRUE);
    if (!stream) {
        opj_destroy_codec(codec);
        return false;
    }

    opj_stream_set_read_function(stream, mem_read_fn);
    opj_stream_set_seek_function(stream, mem_seek_fn);
    opj_stream_set_skip_function(stream, mem_skip_fn);
    opj_stream_set_user_data(stream, &memStream, mem_free_user_data_fn);
    opj_stream_set_user_data_length(stream, size);

    opj_image_t* image = nullptr;
    if (!opj_read_header(stream, codec, &image) || !image) {
        opj_stream_destroy(stream);
        opj_destroy_codec(codec);
        return false;
    }

    if (!opj_decode(codec, stream, image)) {
        opj_image_destroy(image);
        opj_stream_destroy(stream);
        opj_destroy_codec(codec);
        return false;
    }

    opj_end_decompress(codec, stream);
    opj_stream_destroy(stream);
    opj_destroy_codec(codec);

    if (!image || image->numcomps == 0 || !image->comps) {
        if (image) opj_image_destroy(image);
        return false;
    }

    int width = image->comps[0].w;
    int height = image->comps[0].h;
    int num_comps = image->numcomps;

    if (width <= 0 || height <= 0 || num_comps <= 0) {
        opj_image_destroy(image);
        return false;
    }

    for (int c = 0; c < num_comps; ++c) {
        if (!image->comps[c].data) {
            opj_image_destroy(image);
            return false;
        }
    }

    int target_channels = (num_comps >= 3) ? 3 : 1;
    if (num_comps == 4) {
        target_channels = 4;
    }

    out_image.width = width;
    out_image.height = height;
    out_image.channels = target_channels;

    size_t total_pixels = static_cast<size_t>(width) * height;
    out_image.data.resize(total_pixels * target_channels);

    for (size_t pixel_idx = 0; pixel_idx < total_pixels; ++pixel_idx) {
        size_t dest_idx = pixel_idx * target_channels;

        for (int c = 0; c < target_channels; ++c) {
            int src_comp = (c < num_comps) ? c : 0;
            int32_t val = image->comps[src_comp].data[pixel_idx];
            int prec = image->comps[src_comp].prec;
            int is_signed = image->comps[src_comp].sgnd;

            if (is_signed) {
                val += (1 << (prec - 1));
            }

            uint8_t byte_val = 0;
            if (prec == 8) {
                byte_val = static_cast<uint8_t>(std::clamp(val, 0, 255));
            } else if (prec > 8) {
                int shift = prec - 8;
                byte_val = static_cast<uint8_t>(std::clamp(val >> shift, 0, 255));
            } else if (prec > 0) {
                int shift = 8 - prec;
                byte_val = static_cast<uint8_t>(std::clamp(val << shift, 0, 255));
            }

            out_image.data[dest_idx + c] = byte_val;
        }
    }

    opj_image_destroy(image);
    return true;
}

// Decode using stb_image (for JPEG/PNG/BMP disguised as .jp2)
static bool decode_with_stb(const uint8_t* data, size_t size, RawImage& out_image) {
    int w = 0, h = 0, ch = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &ch, 0);
    if (!pixels || w <= 0 || h <= 0 || ch <= 0) {
        if (pixels) stbi_image_free(pixels);
        return false;
    }

    out_image.width = w;
    out_image.height = h;
    out_image.channels = ch;
    size_t total_bytes = static_cast<size_t>(w) * h * ch;
    out_image.data.assign(pixels, pixels + total_bytes);
    stbi_image_free(pixels);
    return true;
}

static fs::path to_clean_path(const std::string& u8str) {
#if defined(_WIN32)
    if (u8str.empty()) return fs::path();
    int size = MultiByteToWideChar(CP_UTF8, 0, u8str.data(), (int)u8str.size(), nullptr, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, u8str.data(), (int)u8str.size(), &wstr[0], size);
    for (auto& ch : wstr) {
        if (ch == L'/') ch = L'\\';
    }
    std::wstring clean;
    for (size_t i = 0; i < wstr.size(); ++i) {
        if (wstr[i] == L'\\' && i > 0 && i + 1 < wstr.size() && wstr[i + 1] == L'\\') {
            continue;
        }
        clean += wstr[i];
    }
    return fs::path(clean);
#else
    return fs::u8path(u8str);
#endif
}

bool JP2Decoder::decode_file(const std::string& filepath, RawImage& out_image) {
    try {
        fs::path p = to_clean_path(filepath);
        if (!fs::exists(p) || !fs::is_regular_file(p)) {
            try {
                p = fs::u8path(filepath);
            } catch (...) {
                p = fs::path(filepath);
            }
        }

        if (!fs::exists(p) || !fs::is_regular_file(p)) {
            return false;
        }

#if defined(_WIN32)
        std::ifstream ifs(p.wstring(), std::ios::binary | std::ios::ate);
#else
        std::ifstream ifs(p, std::ios::binary | std::ios::ate);
#endif
        if (!ifs.is_open()) {
            return false;
        }
        std::streamsize fileSize = ifs.tellg();
        if (fileSize <= 0 || fileSize > 1024LL * 1024LL * 1024LL) {
            return false;
        }
        ifs.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
        if (!ifs.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
            return false;
        }
        ifs.close();

        const uint8_t* data = buffer.data();
        size_t size = buffer.size();

        // 1. Check Magic Header: Standard JPEG (FF D8 FF)
        if (size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
            if (decode_with_stb(data, size, out_image)) {
                return true;
            }
        }

        // 2. Check Magic Header: Standard PNG (89 50 4E 47)
        if (size >= 4 && data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
            if (decode_with_stb(data, size, out_image)) {
                return true;
            }
        }

        // 3. Check Magic Header: Raw J2K Codestream (FF 4F FF 51)
        if (size >= 4 && data[0] == 0xFF && data[1] == 0x4F && data[2] == 0xFF && data[3] == 0x51) {
            if (decode_with_openjpeg(data, size, OPJ_CODEC_J2K, out_image)) {
                return true;
            }
        }

        // 4. Check Magic Header: Standard JP2 Box (00 00 00 0C 6A 50 20 20 0D 0A 87 0A)
        if (size >= 12 && data[4] == 0x6A && data[5] == 0x50 && data[6] == 0x20 && data[7] == 0x20) {
            if (decode_with_openjpeg(data, size, OPJ_CODEC_JP2, out_image)) {
                return true;
            }
        }

        // 5. Fallback auto-detection: Try JP2 -> J2K -> STB
        if (decode_with_openjpeg(data, size, OPJ_CODEC_JP2, out_image)) {
            return true;
        }
        if (decode_with_openjpeg(data, size, OPJ_CODEC_J2K, out_image)) {
            return true;
        }
        if (decode_with_stb(data, size, out_image)) {
            return true;
        }

        return false;
    } catch (...) {
        return false;
    }
}
