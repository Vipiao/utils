// TileableNoiseMap.cpp
#include "TileableNoiseMap.h"
#include "PerlinNoise.h"
#include "utils/HashFunctions.h"
#include <cassert>
#include <cmath>
#include <algorithm>

// Rounds a value in [0, 1] onto the 16-bit unorm range.
static uint16_t toUnorm16(double unitValue) {
    const double scaled{std::round(std::clamp(unitValue, 0.0, 1.0) * 65535.0)};
    return static_cast<uint16_t>(scaled);
}

TileableNoiseMap::TileableNoiseMap(const TileableNoiseMapConfig& config)
    : m_config{config} {
    assert(m_config.m_resolution > 1 && "A map needs at least two texels to be differenced");
    assert(m_config.m_octaveCount >= 1 && "A map with no octaves is flat");
    assert(m_config.m_baseFrequency >= 1 && "The coarsest octave needs at least one cell");
    assert(m_config.m_gain > 0.0 && m_config.m_gain < 1.0 &&
           "Gain outside (0, 1) either kills the fine octaves or lets them dominate");

    // The finest octave decides whether the map can be differenced at all.
    const int64_t topFrequency{static_cast<int64_t>(m_config.m_baseFrequency)
                               << (m_config.m_octaveCount - 1)};
    assert(topFrequency * k_minTexelsPerCell <= m_config.m_resolution &&
           "Finest octave is too small to difference: raise the resolution, or drop "
           "octaves or base frequency");

    generateField();
    generateGradient();
}

int TileableNoiseMap::wrapTexel(int index) const {
    const int remainder{index % m_config.m_resolution};
    return remainder < 0 ? remainder + m_config.m_resolution : remainder;
}

int TileableNoiseMap::texelIndex(int x, int y) const {
    return wrapTexel(y) * m_config.m_resolution + wrapTexel(x);
}

double TileableNoiseMap::sample(int x, int y) const {
    return m_field[texelIndex(x, y)];
}

glm::dvec2 TileableNoiseMap::gradient(int x, int y) const {
    return m_gradient[texelIndex(x, y)];
}

void TileableNoiseMap::generateField() {
    const int resolution{m_config.m_resolution};
    m_field.assign(static_cast<size_t>(resolution) * resolution, 0.0);

    for (int octave{0}; octave < m_config.m_octaveCount; ++octave) {
        const int64_t frequency{static_cast<int64_t>(m_config.m_baseFrequency) << octave};
        const double weight{std::pow(m_config.m_gain, octave)};
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
                m_field[static_cast<size_t>(y) * resolution + x] +=
                    weight * PerlinNoise::sample(point, frequency, octaveSeed);
            }
        }
    }

    // Rescaled to span exactly [0, 1]. The octave weights above are therefore
    // only ever read against each other, and their sum needs no normalizing of
    // its own.
    const auto [minimum, maximum]{std::minmax_element(m_field.begin(), m_field.end())};
    const double span{*maximum - *minimum};
    // A field with no relief has no span to divide by; flatten it to zero.
    const double inverseSpan{span > 0.0 ? 1.0 / span : 0.0};
    const double fieldMinimum{*minimum};

    for (double& value : m_field) {
        value = (value - fieldMinimum) * inverseSpan;
    }
}

void TileableNoiseMap::generateGradient() {
    const int resolution{m_config.m_resolution};
    m_gradient.assign(static_cast<size_t>(resolution) * resolution, glm::dvec2{0.0});

    // Central differences against the tile's own unit span rather than against
    // its texels, so what comes out is independent of the resolution and a
    // caller needs to know neither the texel spacing nor the map's size to use
    // it.
    const double inverseSpan{resolution / 2.0};

    for (int y{0}; y < resolution; ++y) {
        for (int x{0}; x < resolution; ++x) {
            m_gradient[static_cast<size_t>(y) * resolution + x] = glm::dvec2{
                (sample(x + 1, y) - sample(x - 1, y)) * inverseSpan,
                (sample(x, y + 1) - sample(x, y - 1)) * inverseSpan};
        }
    }
}

std::vector<uint16_t> TileableNoiseMap::bake() const {
    std::vector<uint16_t> texels(m_field.size());
    for (size_t texel{0}; texel < m_field.size(); ++texel) {
        texels[texel] = toUnorm16(m_field[texel]);
    }
    return texels;
}

std::vector<float> TileableNoiseMap::bakeGradient() const {
    std::vector<float> texels(m_gradient.size() * 2);

    for (size_t texel{0}; texel < m_gradient.size(); ++texel) {
        for (int axis{0}; axis < 2; ++axis) {
            texels[texel * 2 + axis] = static_cast<float>(m_gradient[texel][axis]);
        }
    }
    return texels;
}
