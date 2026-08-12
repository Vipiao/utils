// TileableHeightMap.cpp
#include "TileableHeightMap.h"
#include "PerlinNoise.h"
#include "utils/HashFunctions.h"
#include <cassert>
#include <cmath>
#include <algorithm>

// Largest and smallest unorm a baked slope may take. The decode divides by
// 1 - |e|, so the endpoints themselves are the slope at infinity.
static constexpr uint16_t s_slopeUnormMin{1};
static constexpr uint16_t s_slopeUnormMax{65534};

// Rounds a value in [0, 1] onto the 16-bit unorm range.
static uint16_t toUnorm16(double unitValue) {
    const double scaled{std::round(glm::clamp(unitValue, 0.0, 1.0) * 65535.0)};
    return static_cast<uint16_t>(scaled);
}

TileableHeightMap::TileableHeightMap(const TileableHeightMapConfig& config)
    : m_config{config} {
    assert(m_config.m_resolution > 1 && "A map needs at least two texels to have a slope");
    assert(m_config.m_octaveCount >= 1 && "A map with no octaves is flat");
    assert(m_config.m_baseFrequency >= 1 && "The coarsest octave needs at least one cell");
    assert(m_config.m_gain > 0.0 && m_config.m_gain < 1.0 &&
           "Gain outside (0, 1) either kills the fine octaves or lets them dominate");
    assert(m_config.m_tileSizeMetres > 0.0 && "Slopes are measured against the tile's size");

    // The finest octave decides whether the map can be differenced at all.
    const int64_t topFrequency{static_cast<int64_t>(m_config.m_baseFrequency)
                               << (m_config.m_octaveCount - 1)};
    assert(topFrequency * k_minTexelsPerCell <= m_config.m_resolution &&
           "Finest octave is too small to difference: raise the resolution, or drop "
           "octaves or base frequency");

    generateElevation();
    generateSlope();
}

int TileableHeightMap::wrapTexel(int index) const {
    const int remainder{index % m_config.m_resolution};
    return remainder < 0 ? remainder + m_config.m_resolution : remainder;
}

int TileableHeightMap::texelIndex(int x, int y) const {
    return wrapTexel(y) * m_config.m_resolution + wrapTexel(x);
}

double TileableHeightMap::elevation(int x, int y) const {
    return m_elevation[texelIndex(x, y)];
}

glm::dvec2 TileableHeightMap::slope(int x, int y) const {
    return m_slope[texelIndex(x, y)];
}

void TileableHeightMap::generateElevation() {
    const int resolution{m_config.m_resolution};
    m_elevation.assign(static_cast<size_t>(resolution) * resolution, 0.0);

    // Normalizing by the summed weights rather than by the extremes actually
    // reached keeps the amplitude a property of the config: two maps built with
    // different seeds are then the same height, not merely similar.
    double weightSum{0.0};
    for (int octave{0}; octave < m_config.m_octaveCount; ++octave) {
        weightSum += std::pow(m_config.m_gain, octave);
    }
    const double normalizeScale{m_config.m_amplitudeMetres / weightSum};

    for (int octave{0}; octave < m_config.m_octaveCount; ++octave) {
        const int64_t frequency{static_cast<int64_t>(m_config.m_baseFrequency)
                                << octave};
        const double weight{std::pow(m_config.m_gain, octave) * normalizeScale};
        // Hashed rather than offset from the seed, so that neighbouring seeds
        // do not hand one map's octave to the next map's.
        const uint64_t octaveSeed{Hash::pcg(m_config.m_seed,
                                            static_cast<uint64_t>(octave))};
        // Cells per texel: a texel's centre in lattice units. The tile spans
        // exactly `frequency` cells, so texel `resolution` lands back on 0.
        const double cellsPerTexel{static_cast<double>(frequency) / resolution};

        for (int y{0}; y < resolution; ++y) {
            for (int x{0}; x < resolution; ++x) {
                const glm::dvec2 point{x * cellsPerTexel, y * cellsPerTexel};
                m_elevation[static_cast<size_t>(y) * resolution + x] +=
                    weight * PerlinNoise::sample(point, frequency, octaveSeed);
            }
        }
    }
}

void TileableHeightMap::generateSlope() {
    const int resolution{m_config.m_resolution};
    m_slope.assign(static_cast<size_t>(resolution) * resolution, glm::dvec2{0.0});

    // Central differences in metres, so what comes out is a true slope and
    // nothing downstream has to know the texel spacing to use it.
    const double metresPerTexel{m_config.m_tileSizeMetres / resolution};
    const double inverseSpan{1.0 / (2.0 * metresPerTexel)};

    for (int y{0}; y < resolution; ++y) {
        for (int x{0}; x < resolution; ++x) {
            m_slope[static_cast<size_t>(y) * resolution + x] = glm::dvec2{
                (elevation(x + 1, y) - elevation(x - 1, y)) * inverseSpan,
                (elevation(x, y + 1) - elevation(x, y - 1)) * inverseSpan};
        }
    }
}

TileableHeightMap::ElevationBake TileableHeightMap::bakeElevation() const {
    const auto [minimum, maximum]{
        std::minmax_element(m_elevation.begin(), m_elevation.end())};

    ElevationBake bake{};
    bake.m_minMetres = *minimum;
    bake.m_rangeMetres = *maximum - *minimum;

    // A field with no relief has no range to normalize against; map it to the
    // bottom of the unorm range and leave the scale harmless.
    const double inverseRange{bake.m_rangeMetres > 0.0 ? 1.0 / bake.m_rangeMetres : 0.0};

    bake.m_texels.resize(m_elevation.size());
    for (size_t texel{0}; texel < m_elevation.size(); ++texel) {
        bake.m_texels[texel] =
            toUnorm16((m_elevation[texel] - bake.m_minMetres) * inverseRange);
    }
    return bake;
}

std::vector<uint16_t> TileableHeightMap::bakeSlope() const {
    std::vector<uint16_t> texels(m_slope.size() * 2);

    for (size_t texel{0}; texel < m_slope.size(); ++texel) {
        for (int axis{0}; axis < 2; ++axis) {
            const double gradient{m_slope[texel][axis]};
            const double squashed{gradient / (1.0 + std::abs(gradient))};
            texels[texel * 2 + axis] =
                std::clamp(toUnorm16(squashed * 0.5 + 0.5), s_slopeUnormMin,
                           s_slopeUnormMax);
        }
    }
    return texels;
}
