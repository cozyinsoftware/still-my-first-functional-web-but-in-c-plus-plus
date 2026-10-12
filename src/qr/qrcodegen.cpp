/*
 * QR Code generator library (C++)
 *
 * Copyright (c) Project Nayuki. (MIT License)
 * https://www.nayuki.io/page/qr-code-generator-library
 */

#include "qrcodegen.hpp"
#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <utility>

namespace qrcodegen {

// ---- QrSegment implementation ----

QrSegment::QrSegment(Mode md, int numCh, const std::vector<bool>& dt) :
    mode(md),
    numChars(numCh),
    data(dt) {
    if (numCh < 0)
        throw std::domain_error("Invalid value");
}

QrSegment::QrSegment(Mode md, int numCh, std::vector<bool>&& dt) :
    mode(md),
    numChars(numCh),
    data(std::move(dt)) {
    if (numCh < 0)
        throw std::domain_error("Invalid value");
}

QrSegment::Mode QrSegment::getMode() const { return mode; }
int QrSegment::getNumChars() const { return numChars; }
const std::vector<bool>& QrSegment::getData() const { return data; }

QrSegment QrSegment::makeBytes(const std::vector<uint8_t>& data) {
    if (data.size() > static_cast<unsigned int>(INT_MAX))
        throw std::length_error("Data too long");
    std::vector<bool> bb;
    for (uint8_t b : data) {
        for (int i = 7; i >= 0; i--)
            bb.push_back(((b >> i) & 1) != 0);
    }
    return QrSegment(Mode::BYTE, static_cast<int>(data.size()), std::move(bb));
}

QrSegment QrSegment::makeBytes(const std::string& data) {
    std::vector<uint8_t> bytes(data.begin(), data.end());
    return makeBytes(bytes);
}

static int numCharCountBits(QrSegment::Mode mode, int version) {
    int i = (version + 7) / 17;
    switch (mode) {
        case QrSegment::Mode::NUMERIC:      { static const int temp[] = {10, 12, 14}; return temp[i]; }
        case QrSegment::Mode::ALPHANUMERIC: { static const int temp[] = { 9, 11, 13}; return temp[i]; }
        case QrSegment::Mode::BYTE:         { static const int temp[] = { 8, 16, 16}; return temp[i]; }
        case QrSegment::Mode::KANJI:        { static const int temp[] = { 8, 10, 12}; return temp[i]; }
        case QrSegment::Mode::ECI:          return 0;
        default:  throw std::logic_error("Unreachable");
    }
}

int QrSegment::getTotalBits(const std::vector<QrSegment>& segs, int version) {
    int result = 0;
    for (const QrSegment& seg : segs) {
        int ccbits = numCharCountBits(seg.mode, version);
        if (seg.numChars >= (1L << ccbits))
            return -1;
        if (4 + ccbits > INT_MAX - result)
            return -1;
        result += 4 + ccbits;
        if (seg.data.size() > static_cast<unsigned int>(INT_MAX - result))
            return -1;
        result += static_cast<int>(seg.data.size());
    }
    return result;
}

// ---- QrCode implementation ----

static const int8_t ECC_CODEWORDS_PER_BLOCK[4][41] = {
    // Version: (note: version 0 does not exist)
    //0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40    Error correction level
    {-1,  7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},  // Low
    {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28},  // Medium
    {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},  // Quartile
    {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},  // High
};

static const int8_t NUM_ERROR_CORRECTION_BLOCKS[4][41] = {
    // Version: (note: version 0 does not exist)
    //0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40    Error correction level
    {-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4,  4,  4,  4,  4,  6,  6,  6,  6,  7,  8,  8,  9,  9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},  // Low
    {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5,  5,  8,  9,  9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},  // Medium
    {-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8,  8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},  // Quartile
    {-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81},  // High
};

static int getNumDataCodewords(int ver, QrCode::Ecc ecl) {
    int v = ver;
    int e = static_cast<int>(ecl);
    return (v * 4 + 17) * (v * 4 + 17) / 8
        - ECC_CODEWORDS_PER_BLOCK[e][v] * NUM_ERROR_CORRECTION_BLOCKS[e][v];
}

static int getNumRawDataModules(int ver) {
    int result = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        int numAlign = ver / 7 + 1;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if (ver >= 7)
            result -= 36;
    }
    return result;
}

QrCode QrCode::encodeText(const char* text, Ecc ecl) {
    std::vector<QrSegment> segs;
    segs.push_back(QrSegment::makeBytes(std::string(text)));
    return encodeSegments(segs, ecl);
}

QrCode QrCode::encodeBinary(const std::vector<uint8_t>& data, Ecc ecl) {
    std::vector<QrSegment> segs;
    segs.push_back(QrSegment::makeBytes(data));
    return encodeSegments(segs, ecl);
}

QrCode QrCode::encodeSegments(const std::vector<QrSegment>& segs, Ecc ecl,
        int minVersion, int maxVersion, int mask, bool boostEcl) {
    if (!(MIN_VERSION <= minVersion && minVersion <= maxVersion && maxVersion <= MAX_VERSION) || mask < -1 || mask > 7)
        throw std::invalid_argument("Invalid value");

    int version, dataUsedBits;
    for (version = minVersion; ; version++) {
        int dataCapacityBits = getNumDataCodewords(version, ecl) * 8;
        dataUsedBits = QrSegment::getTotalBits(segs, version);
        if (dataUsedBits != -1 && dataUsedBits <= dataCapacityBits)
            break;
        if (version >= maxVersion) {
            throw std::length_error("Data too long for QR Code");
        }
    }

    if (boostEcl) {
        for (Ecc newEcl : {Ecc::MEDIUM, Ecc::QUARTILE, Ecc::HIGH}) {
            if (dataUsedBits <= getNumDataCodewords(version, newEcl) * 8)
                ecl = newEcl;
        }
    }

    std::vector<bool> bb;
    for (const QrSegment& seg : segs) {
        int modeBits = static_cast<int>(seg.getMode());
        for (int i = 3; i >= 0; i--)
            bb.push_back(((modeBits >> i) & 1) != 0);
        int ccbits = numCharCountBits(seg.getMode(), version);
        for (int i = ccbits - 1; i >= 0; i--)
            bb.push_back(((seg.getNumChars() >> i) & 1) != 0);
        for (bool b : seg.getData())
            bb.push_back(b);
    }

    int dataCapacityBits = getNumDataCodewords(version, ecl) * 8;
    int terminatorBits = std::min(4, dataCapacityBits - static_cast<int>(bb.size()));
    for (int i = 0; i < terminatorBits; i++)
        bb.push_back(false);
    while (bb.size() % 8 != 0)
        bb.push_back(false);

    for (uint8_t padByte = 0xEC; bb.size() < static_cast<size_t>(dataCapacityBits); padByte ^= 0xEC ^ 0x11) {
        for (int i = 7; i >= 0; i--)
            bb.push_back(((padByte >> i) & 1) != 0);
    }

    std::vector<uint8_t> dataCodewords(bb.size() / 8);
    for (size_t i = 0; i < bb.size(); i++)
        dataCodewords[i / 8] |= (bb[i] ? 1 : 0) << (7 - (i % 8));

    return QrCode(version, ecl, dataCodewords, mask);
}

QrCode::QrCode(int ver, Ecc ecl, const std::vector<uint8_t>& dataCodewords, int msk) :
    version(ver),
    size(ver * 4 + 17),
    errorCorrectionLevel(ecl),
    mask(msk),
    modules(size, std::vector<bool>(size, false)),
    isFunction(size, std::vector<bool>(size, false)) {

    drawFunctionPatterns();
    std::vector<uint8_t> allCodewords = addEccAndInterleave(dataCodewords);
    drawCodewords(allCodewords);

    if (mask == -1) {
        long minPenalty = LONG_MAX;
        for (int i = 0; i < 8; i++) {
            applyMask(i);
            drawFormatBits(i);
            long penalty = getPenaltyScore();
            if (penalty < minPenalty) {
                mask = i;
                minPenalty = penalty;
            }
            applyMask(i);
        }
    }
    applyMask(mask);
    drawFormatBits(mask);
}

int QrCode::getVersion() const { return version; }
int QrCode::getSize() const { return size; }
QrCode::Ecc QrCode::getErrorCorrectionLevel() const { return errorCorrectionLevel; }
int QrCode::getMask() const { return mask; }
bool QrCode::getModule(int x, int y) const {
    return 0 <= x && x < size && 0 <= y && y < size && module(x, y);
}

bool QrCode::module(int x, int y, bool dflt) const {
    if (0 <= x && x < size && 0 <= y && y < size)
        return modules[y][x];
    return dflt;
}

void QrCode::setFunctionModule(int x, int y, bool isDark) {
    modules[y][x] = isDark;
    isFunction[y][x] = true;
}

void QrCode::drawFinderPattern(int x, int y) {
    for (int dy = -4; dy <= 4; dy++) {
        for (int dx = -4; dx <= 4; dx++) {
            int dist = std::max(std::abs(dx), std::abs(dy));
            int xx = x + dx, yy = y + dy;
            if (0 <= xx && xx < size && 0 <= yy && yy < size)
                setFunctionModule(xx, yy, dist != 2 && dist != 4);
        }
    }
}

void QrCode::drawAlignmentPattern(int x, int y) {
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++)
            setFunctionModule(x + dx, y + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
    }
}

void QrCode::drawFunctionPatterns() {
    for (int i = 0; i < size; i++) {
        setFunctionModule(6, i, i % 2 == 0);
        setFunctionModule(i, 6, i % 2 == 0);
    }
    drawFinderPattern(3, 3);
    drawFinderPattern(size - 4, 3);
    drawFinderPattern(3, size - 4);

    if (version >= 2) {
        int numAlign = version / 7 + 1;
        int step = (version == 32) ? 26 : (version * 4 + numAlign * 2 + 1) / (numAlign * 2 - 2) * 2;
        std::vector<int> alignPatPos(numAlign);
        alignPatPos[0] = 6;
        for (int i = numAlign - 1, pos = size - 7; i >= 1; i--, pos -= step)
            alignPatPos[i] = pos;

        for (size_t i = 0; i < alignPatPos.size(); i++) {
            for (size_t j = 0; j < alignPatPos.size(); j++) {
                if (!((i == 0 && j == 0) || (i == 0 && j == alignPatPos.size() - 1) || (i == alignPatPos.size() - 1 && j == 0)))
                    drawAlignmentPattern(alignPatPos[i], alignPatPos[j]);
            }
        }
    }

    drawFormatBits(0);
    drawVersion();
}

void QrCode::drawFormatBits(int msk) {
    int data = static_cast<int>(errorCorrectionLevel) ^ 1;
    data = (data << 3) | msk;
    int rem = data;
    for (int i = 0; i < 10; i++)
        rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    int bits = ((data << 10) | rem) ^ 0x5412;

    for (int i = 0; i <= 5; i++)
        setFunctionModule(8, i, ((bits >> i) & 1) != 0);
    setFunctionModule(8, 7, ((bits >> 6) & 1) != 0);
    setFunctionModule(8, 8, ((bits >> 7) & 1) != 0);
    setFunctionModule(7, 8, ((bits >> 8) & 1) != 0);
    for (int i = 9; i < 15; i++)
        setFunctionModule(14 - i, 8, ((bits >> i) & 1) != 0);

    for (int i = 0; i < 8; i++)
        setFunctionModule(size - 1 - i, 8, ((bits >> i) & 1) != 0);
    for (int i = 8; i < 15; i++)
        setFunctionModule(8, size - 15 + i, ((bits >> i) & 1) != 0);
    setFunctionModule(8, size - 8, true);
}

void QrCode::drawVersion() {
    if (version < 7)
        return;
    int rem = version;
    for (int i = 0; i < 12; i++)
        rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
    long bits = (static_cast<long>(version) << 12) | rem;
    for (int i = 0; i < 18; i++) {
        bool bit = ((bits >> i) & 1) != 0;
        int a = size - 11 + i % 3, b = i / 3;
        setFunctionModule(a, b, bit);
        setFunctionModule(b, a, bit);
    }
}

static uint8_t reedSolomonMultiply(uint8_t x, uint8_t y) {
    int z = 0;
    for (int i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return static_cast<uint8_t>(z);
}

static std::vector<uint8_t> reedSolomonComputeDivisor(int degree) {
    std::vector<uint8_t> result(degree, 0);
    result[degree - 1] = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; i++) {
        for (size_t j = 0; j < result.size(); j++) {
            result[j] = reedSolomonMultiply(result[j], root);
            if (j + 1 < result.size())
                result[j] ^= result[j + 1];
        }
        root = reedSolomonMultiply(root, 0x02);
    }
    return result;
}

static std::vector<uint8_t> reedSolomonComputeRemainder(const std::vector<uint8_t>& data, const std::vector<uint8_t>& divisor) {
    std::vector<uint8_t> result(divisor.size(), 0);
    for (uint8_t b : data) {
        uint8_t factor = b ^ result[0];
        result.erase(result.begin());
        result.push_back(0);
        for (size_t i = 0; i < divisor.size(); i++)
            result[i] ^= reedSolomonMultiply(divisor[i], factor);
    }
    return result;
}

std::vector<uint8_t> QrCode::addEccAndInterleave(const std::vector<uint8_t>& data) const {
    int numBlocks = NUM_ERROR_CORRECTION_BLOCKS[static_cast<int>(errorCorrectionLevel)][version];
    int blockEccLen = ECC_CODEWORDS_PER_BLOCK[static_cast<int>(errorCorrectionLevel)][version];
    int rawCodewords = getNumRawDataModules(version) / 8;
    int numShortBlocks = numBlocks - rawCodewords % numBlocks;
    int shortBlockLen = rawCodewords / numBlocks;

    std::vector<std::vector<uint8_t>> blocks;
    std::vector<uint8_t> rsDiv = reedSolomonComputeDivisor(blockEccLen);
    for (int i = 0, k = 0; i < numBlocks; i++) {
        std::vector<uint8_t> dat(data.begin() + k, data.begin() + (k + shortBlockLen - blockEccLen + (i >= numShortBlocks ? 1 : 0)));
        k += static_cast<int>(dat.size());
        std::vector<uint8_t> ecc = reedSolomonComputeRemainder(dat, rsDiv);
        dat.insert(dat.end(), ecc.begin(), ecc.end());
        blocks.push_back(std::move(dat));
    }

    std::vector<uint8_t> result;
    for (size_t i = 0; i < blocks[0].size(); i++) {
        for (size_t j = 0; j < blocks.size(); j++) {
            if (i < blocks[j].size())
                result.push_back(blocks[j][i]);
        }
    }
    return result;
}

void QrCode::drawCodewords(const std::vector<uint8_t>& data) {
    size_t i = 0;
    for (int right = size - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int vert = 0; vert < size; vert++) {
            for (int j = 0; j < 2; j++) {
                int x = right - j;
                bool upward = ((right + 1) & 2) == 0;
                int y = upward ? size - 1 - vert : vert;
                if (!isFunction[y][x] && i < data.size() * 8) {
                    modules[y][x] = ((data[i / 8] >> (7 - (i % 8))) & 1) != 0;
                    i++;
                }
            }
        }
    }
}

void QrCode::applyMask(int msk) {
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            bool invert;
            switch (msk) {
                case 0:  invert = (x + y) % 2 == 0;                    break;
                case 1:  invert = y % 2 == 0;                          break;
                case 2:  invert = x % 3 == 0;                          break;
                case 3:  invert = (x + y) % 3 == 0;                    break;
                case 4:  invert = (x / 3 + y / 2) % 2 == 0;            break;
                case 5:  invert = x * y % 2 + x * y % 3 == 0;          break;
                case 6:  invert = (x * y % 2 + x * y % 3) % 2 == 0;    break;
                case 7:  invert = ((x + y) % 2 + x * y % 3) % 2 == 0;  break;
                default: throw std::logic_error("Unreachable");
            }
            modules[y][x] = modules[y][x] ^ (invert && !isFunction[y][x]);
        }
    }
}

long QrCode::getPenaltyScore() const {
    long result = 0;
    for (int y = 0; y < size; y++) {
        bool runColor = false;
        int runX = 0;
        for (int x = 0; x < size; x++) {
            if (module(x, y) == runColor) {
                runX++;
                if (runX == 5) result += 3;
                else if (runX > 5) result++;
            } else {
                runColor = module(x, y);
                runX = 1;
            }
        }
    }
    for (int x = 0; x < size; x++) {
        bool runColor = false;
        int runY = 0;
        for (int y = 0; y < size; y++) {
            if (module(x, y) == runColor) {
                runY++;
                if (runY == 5) result += 3;
                else if (runY > 5) result++;
            } else {
                runColor = module(x, y);
                runY = 1;
            }
        }
    }
    for (int y = 0; y < size - 1; y++) {
        for (int x = 0; x < size - 1; x++) {
            bool color = module(x, y);
            if (color == module(x + 1, y) && color == module(x, y + 1) && color == module(x + 1, y + 1))
                result += 3;
        }
    }
    int dark = 0;
    for (const auto& row : modules) {
        for (bool color : row) {
            if (color) dark++;
        }
    }
    int total = size * size;
    int k = static_cast<int>((std::abs(dark * 20L - total * 10L) + total - 1) / total) - 1;
    result += k * 10;
    return result;
}

} // namespace qrcodegen
