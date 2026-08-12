// TileableNoiseMap.h
#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

/**
 * @brief What shape of noise to generate. See TileableNoiseMap.
 */
struct TileableNoiseMapConfig {
    // Texels per side. The map is always square.
    int m_resolution{1024};
    // How many octaves are summed. Each is twice the frequency of the last.
    int m_octaveCount{7};
    // Lattice cells across the tile at octave 0. An integer because tiling
    // needs every octave's period to be a whole number of cells, and octaves
    // double in frequency, so only an integer base keeps that true all the way
    // up.
    int m_baseFrequency{2};
    // Amplitude of each octave relative to the one before it. At 0.5 every
    // octave contributes the same amount of *slope*, since amplitude halving
    // and frequency doubling cancel, and the surface reads as fuzz. Below that
    // the coarse octaves shape the field and the fine ones texture it.
    double m_gain{0.45};
    uint64_t m_seed{0};
};

/**
 * @brief A square, tileable field summed from octaves of Perlin noise.
 *
 * Dimensionless throughout. The field is rescaled to span exactly [0, 1], so
 * the map carries its own normalization in its values and needs no constants
 * alongside it to be read back: 0 is the minimum, 1 the maximum, for every
 * seed. What the field means -- elevation, density, mask -- and what it is
 * measured in are decided entirely by whoever samples it.
 *
 * Rescaling against the extremes actually reached rather than against the
 * summed octave weights is what makes the range exact rather than nominal, at
 * the cost of letting a lone outlier set the scale and compress the rest.
 *
 * Deterministic: the same config always produces the same map.
 *
 * Knows nothing about GL, spheres, or how the map will be projected.
 */
class TileableNoiseMap {
public:
    explicit TileableNoiseMap(const TileableNoiseMapConfig& config);

    const TileableNoiseMapConfig& config() const { return m_config; }

    // Field value at a texel, in [0, 1]. Indices wrap.
    double sample(int x, int y) const;
    // Gradient of the field at a texel, per unit of tile: the tile spans one
    // unit on each axis whatever the resolution, so this is dimensionless like
    // the field itself and a caller scales it by its own height over its own
    // width. Indices wrap.
    glm::dvec2 gradient(int x, int y) const;

    // The field quantized to 16-bit unorm, row major. Fixed point rather than
    // half float because the field is bounded, so uniform absolute precision
    // beats a mantissa that coarsens with magnitude.
    std::vector<uint16_t> bake() const;

    /**
     * @brief The gradient as a float pair per texel, row major.
     *
     * Unnormalized, and float rather than fixed point because a gradient has no
     * bound the field itself implies: fixed point could only hold it by being
     * given a range to map onto, which the map would then have to carry around
     * to be read back. A float spends its precision proportionally instead,
     * which is where a gradient wants it -- the normal it feeds turns fastest
     * per unit of gradient near flat.
     *
     * Linear in the quantity it stores, so an average of the values is the
     * average of the gradients. A mip chain built over this holds the mean
     * gradient across each footprint rather than something an encoding curve
     * has bent.
     *
     * Baked rather than left to be differenced from the map later because the
     * central difference taken here can use the unquantized field.
     */
    std::vector<float> bakeGradient() const;

private:
    // Texels per lattice cell the finest octave must have. A sampler that
    // differences the map for a gradient runs out of resolution about an octave
    // before the field itself does, and this is the bound that leaves it room.
    static constexpr int k_minTexelsPerCell{8};

    TileableNoiseMapConfig m_config{};
    std::vector<double> m_field;
    std::vector<glm::dvec2> m_gradient;

    int wrapTexel(int index) const;
    int texelIndex(int x, int y) const;

    void generateField();
    void generateGradient();
};
