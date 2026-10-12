#include "qr_service.hpp"
#include "qrcodegen.hpp"
#include "../utils/base64.hpp"

#include <vector>
#include <cstdint>
#include <algorithm>

namespace qr {

namespace {

// Standard PNG CRC32 table & calculation
uint32_t crc32_for_byte(uint32_t byte_val) {
    uint32_t c = byte_val;
    for (int k = 0; k < 8; ++k) {
        c = (c & 1) ? (0xEDB88320L ^ (c >> 1)) : (c >> 1);
    }
    return c;
}

uint32_t update_crc32(uint32_t crc, const uint8_t* buf, size_t len) {
    static uint32_t crc_table[256];
    static bool table_computed = false;
    if (!table_computed) {
        for (int n = 0; n < 256; ++n) {
            crc_table[n] = crc32_for_byte(n);
        }
        table_computed = true;
    }

    uint32_t c = crc ^ 0xFFFFFFFFL;
    for (size_t i = 0; i < len; ++i) {
        c = crc_table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFL;
}

// Adler32 for Zlib wrapper
uint32_t update_adler32(uint32_t adler, const uint8_t* data, size_t len) {
    const uint32_t MOD_ADLER = 65521;
    uint32_t a = adler & 0xFFFF;
    uint32_t b = (adler >> 16) & 0xFFFF;

    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % MOD_ADLER;
        b = (b + a) % MOD_ADLER;
    }
    return (b << 16) | a;
}

void write_be32(std::vector<uint8_t>& out, uint32_t val) {
    out.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(val & 0xFF));
}

void write_le16(std::vector<uint8_t>& out, uint16_t val) {
    out.push_back(static_cast<uint8_t>(val & 0xFF));
    out.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
}

void write_chunk(std::vector<uint8_t>& out, const char type[4], const uint8_t* data, size_t len) {
    write_be32(out, static_cast<uint32_t>(len));
    size_t type_start = out.size();
    out.push_back(static_cast<uint8_t>(type[0]));
    out.push_back(static_cast<uint8_t>(type[1]));
    out.push_back(static_cast<uint8_t>(type[2]));
    out.push_back(static_cast<uint8_t>(type[3]));

    if (len > 0 && data != nullptr) {
        out.insert(out.end(), data, data + len);
    }

    uint32_t crc = update_crc32(0, out.data() + type_start, len + 4);
    write_be32(out, crc);
}

} // namespace

std::vector<uint8_t> QrService::create_png(
    const std::vector<uint8_t>& rgb_data,
    int width,
    int height
) {
    std::vector<uint8_t> png;

    // 1. PNG Signature
    static const uint8_t PNG_SIGNATURE[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    png.insert(png.end(), PNG_SIGNATURE, PNG_SIGNATURE + 8);

    // 2. IHDR Chunk
    std::vector<uint8_t> ihdr;
    write_be32(ihdr, width);
    write_be32(ihdr, height);
    ihdr.push_back(8); // Bit depth: 8
    ihdr.push_back(2); // Color type: 2 (RGB)
    ihdr.push_back(0); // Compression method: 0
    ihdr.push_back(0); // Filter method: 0
    ihdr.push_back(0); // Interlace method: 0
    write_chunk(png, "IHDR", ihdr.data(), ihdr.size());

    // 3. IDAT Chunk: construct scanlines with filter byte 0x00
    std::vector<uint8_t> raw_scanlines;
    raw_scanlines.reserve(height * (width * 3 + 1));
    for (int y = 0; y < height; ++y) {
        raw_scanlines.push_back(0); // Filter byte: None
        const uint8_t* row_pixels = &rgb_data[y * width * 3];
        raw_scanlines.insert(raw_scanlines.end(), row_pixels, row_pixels + width * 3);
    }

    // Wrap raw scanlines in Zlib stream with uncompressed DEFLATE blocks
    std::vector<uint8_t> idat;
    idat.push_back(0x78); // Zlib CMF
    idat.push_back(0x01); // Zlib FLG (FCHECK set so (0x7801 % 31 == 0))

    size_t offset = 0;
    size_t total = raw_scanlines.size();
    while (offset < total) {
        size_t block_len = std::min(total - offset, static_cast<size_t>(65535));
        bool is_last = (offset + block_len == total);

        idat.push_back(is_last ? 0x01 : 0x00); // BFINAL | BTYPE=00
        write_le16(idat, static_cast<uint16_t>(block_len));
        write_le16(idat, static_cast<uint16_t>(~block_len));

        idat.insert(idat.end(), raw_scanlines.data() + offset, raw_scanlines.data() + offset + block_len);
        offset += block_len;
    }

    // Adler-32
    uint32_t adler = update_adler32(1, raw_scanlines.data(), raw_scanlines.size());
    write_be32(idat, adler);

    write_chunk(png, "IDAT", idat.data(), idat.size());

    // 4. IEND Chunk
    write_chunk(png, "IEND", nullptr, 0);

    return png;
}

std::string QrService::generate_qr_data_uri(
    const std::string& text,
    int border,
    int box_size,
    uint32_t fill_color,
    uint32_t back_color
) {
    qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);

    int qr_size = qr.getSize();
    int full_size = (qr_size + border * 2) * box_size;

    uint8_t fill_r = (fill_color >> 16) & 0xFF;
    uint8_t fill_g = (fill_color >> 8) & 0xFF;
    uint8_t fill_b = fill_color & 0xFF;

    uint8_t back_r = (back_color >> 16) & 0xFF;
    uint8_t back_g = (back_color >> 8) & 0xFF;
    uint8_t back_b = back_color & 0xFF;

    std::vector<uint8_t> rgb(full_size * full_size * 3);

    for (int y = 0; y < full_size; ++y) {
        int qr_y = (y / box_size) - border;
        for (int x = 0; x < full_size; ++x) {
            int qr_x = (x / box_size) - border;

            bool is_dark = false;
            if (qr_x >= 0 && qr_x < qr_size && qr_y >= 0 && qr_y < qr_size) {
                is_dark = qr.getModule(qr_x, qr_y);
            }

            size_t idx = (y * full_size + x) * 3;
            if (is_dark) {
                rgb[idx + 0] = fill_r;
                rgb[idx + 1] = fill_g;
                rgb[idx + 2] = fill_b;
            } else {
                rgb[idx + 0] = back_r;
                rgb[idx + 1] = back_g;
                rgb[idx + 2] = back_b;
            }
        }
    }

    std::vector<uint8_t> png_bytes = create_png(rgb, full_size, full_size);
    std::string b64 = utils::base64_encode(png_bytes.data(), png_bytes.size());
    return "data:image/png;base64," + b64;
}

} // namespace qr
