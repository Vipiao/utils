// PngWriter.cpp
#include "PngWriter.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <fstream>
#include <ostream>

namespace {

// PNG's names for the two layouts written here, and the samples each texel takes.
constexpr std::uint8_t k_greyColourType{0};
constexpr std::uint8_t k_rgbColourType{2};
constexpr int k_greySamples{1};
constexpr int k_rgbSamples{3};

// What a deflate stored block can carry, its length being two bytes wide.
constexpr std::size_t k_storedBlockMax{65535};

// The eight bytes that open every PNG. The first is outside ASCII and the pair
// after the name is a line ending, so a file mangled by a text-mode transfer
// fails to open rather than opening wrong.
constexpr std::uint8_t k_signature[]{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

// PNG's checksum over a chunk, and zlib's over the samples. Computed a bit and a
// byte at a time rather than through tables: this runs once per file, against
// file I/O, and a table would be the only state in the module.
std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t previous) {
    std::uint32_t crc{~previous};
    for (std::size_t index{0}; index < size; ++index) {
        crc ^= data[index];
        for (int bit{0}; bit < 8; ++bit) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }

    return ~crc;
}

std::uint32_t adler32(const std::vector<std::uint8_t>& data) {
    constexpr std::uint32_t k_modulus{65521};

    std::uint32_t low{1};
    std::uint32_t high{0};
    for (std::uint8_t byte : data) {
        low = (low + byte) % k_modulus;
        high = (high + low) % k_modulus;
    }

    return (high << 16) | low;
}

void appendBigEndian(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

// Length, type, data, checksum. The checksum covers the type and the data but not
// the length, which is why it is taken in two passes.
void writeChunk(std::ostream& file, const char* type,
                const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> length;
    appendBigEndian(length, static_cast<std::uint32_t>(data.size()));
    file.write(reinterpret_cast<const char*>(length.data()), 4);
    file.write(type, 4);
    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));

    std::uint32_t crc{crc32(reinterpret_cast<const std::uint8_t*>(type), 4, 0)};
    crc = crc32(data.data(), data.size(), crc);

    std::vector<std::uint8_t> checksum;
    appendBigEndian(checksum, crc);
    file.write(reinterpret_cast<const char*>(checksum.data()), 4);
}

// The samples as PNG wants them before compression: a filter byte in front of
// every row, then the row's samples high byte first. Filter zero throughout,
// meaning the samples stand as they are, since nothing here is compressing them
// afterwards for a filter to help.
std::vector<std::uint8_t> rawScanlines(int width, int height, int samplesPerTexel,
                                       const std::vector<std::uint16_t>& samples) {
    const std::size_t rowSamples{static_cast<std::size_t>(width) * samplesPerTexel};

    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1 + rowSamples * 2));
    for (int y{0}; y < height; ++y) {
        raw.push_back(0);

        const std::uint16_t* row{samples.data() + static_cast<std::size_t>(y) * rowSamples};
        for (std::size_t index{0}; index < rowSamples; ++index) {
            raw.push_back(static_cast<std::uint8_t>(row[index] >> 8));
            raw.push_back(static_cast<std::uint8_t>(row[index] & 0xFF));
        }
    }

    return raw;
}

// A zlib stream carrying the bytes uncompressed: the two byte header deflate is
// wrapped in, the bytes in stored blocks, and the running checksum.
std::vector<std::uint8_t> storedZlibStream(const std::vector<std::uint8_t>& raw) {
    std::vector<std::uint8_t> stream;
    stream.reserve(raw.size() + raw.size() / k_storedBlockMax * 5 + 16);

    // Deflate at the smallest window, which stored blocks never reach into, and
    // the check bits that make the pair a multiple of thirty-one.
    stream.push_back(0x78);
    stream.push_back(0x01);

    std::size_t offset{0};
    do {
        const std::size_t size{std::min(k_storedBlockMax, raw.size() - offset)};
        const bool last{offset + size == raw.size()};

        stream.push_back(last ? 1 : 0);
        stream.push_back(static_cast<std::uint8_t>(size & 0xFF));
        stream.push_back(static_cast<std::uint8_t>(size >> 8));
        stream.push_back(static_cast<std::uint8_t>(~size & 0xFF));
        stream.push_back(static_cast<std::uint8_t>((~size >> 8) & 0xFF));
        stream.insert(stream.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                      raw.begin() + static_cast<std::ptrdiff_t>(offset + size));

        offset += size;
    } while (offset < raw.size());

    appendBigEndian(stream, adler32(raw));

    return stream;
}

bool write(const std::filesystem::path& path, int width, int height,
           std::uint8_t colourType, int samplesPerTexel,
           const std::vector<std::uint16_t>& samples) {
    assert(width > 0 && height > 0 && "An image needs both of its sides");
    assert(samples.size() ==
               static_cast<std::size_t>(width) * height * samplesPerTexel &&
           "The samples must be exactly what the size and the layout name");

    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    if (!file) {
        return false;
    }

    file.write(reinterpret_cast<const char*>(k_signature), sizeof(k_signature));

    std::vector<std::uint8_t> header;
    appendBigEndian(header, static_cast<std::uint32_t>(width));
    appendBigEndian(header, static_cast<std::uint32_t>(height));
    header.push_back(16);           // bits per sample
    header.push_back(colourType);
    header.push_back(0);            // deflate, the only compression PNG defines
    header.push_back(0);            // the filter set the row bytes above choose from
    header.push_back(0);            // rows in order rather than interlaced
    writeChunk(file, "IHDR", header);

    writeChunk(file, "IDAT",
               storedZlibStream(rawScanlines(width, height, samplesPerTexel, samples)));
    writeChunk(file, "IEND", std::vector<std::uint8_t>{});

    return static_cast<bool>(file);
}

}   // namespace

bool PngWriter::writeGrey16(const std::filesystem::path& path, int width, int height,
                            const std::vector<std::uint16_t>& samples) {
    return write(path, width, height, k_greyColourType, k_greySamples, samples);
}

bool PngWriter::writeRgb16(const std::filesystem::path& path, int width, int height,
                           const std::vector<std::uint16_t>& samples) {
    return write(path, width, height, k_rgbColourType, k_rgbSamples, samples);
}
