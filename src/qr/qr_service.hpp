#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace qr {

class QrService {
public:
    // Generates a base64 Data URI ("data:image/png;base64,...") for the given URL or text
    static std::string generate_qr_data_uri(
        const std::string& text,
        int border = 4,
        int box_size = 10,
        uint32_t fill_color = 0x111827, // Dark slate/gray from app.py (#111827)
        uint32_t back_color = 0xFFFFFF  // White
    );

    // Encodes raw RGB pixels (3 bytes per pixel) into a valid PNG byte vector
    static std::vector<uint8_t> create_png(
        const std::vector<uint8_t>& rgb_data,
        int width,
        int height
    );
};

} // namespace qr
