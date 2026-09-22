// PngWriter.h
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

/**
 * @brief Sixteen-bit PNG, written without a compression library.
 *
 * For looking at a field a program just produced: a map, a mask, a gradient. The
 * point is that the image opens anywhere, so the format is PNG rather than one of
 * the header-and-samples formats that would be shorter to write.
 *
 * Sixteen bits per sample throughout, because the fields this is for are computed
 * rather than photographed and eight would band them visibly.
 *
 * The samples are stored rather than compressed: deflate's stored block needs no
 * encoder, and a file this is written to is read once by a person and deleted.
 * The result is a valid PNG about as large as the samples themselves.
 *
 * Knows nothing about what the samples mean or what range they came from.
 */
namespace PngWriter {

// One sample per texel, row major. False if the file could not be written.
bool writeGrey16(const std::filesystem::path& path, int width, int height,
                 const std::vector<std::uint16_t>& samples);

// Three samples per texel, red, green then blue, row major.
bool writeRgb16(const std::filesystem::path& path, int width, int height,
                const std::vector<std::uint16_t>& samples);

}   // namespace PngWriter
