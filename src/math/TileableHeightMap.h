// TileableHeightMap.h
#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

/**
 * @brief What shape of terrain to generate. See TileableHeightMap.
 */
struct TileableHeightMapConfig {
    // Texels per side. The map is always square.
    int m_resolution{1024};
    // How many octaves are summed. Each is twice the frequency of the last.
    int m_octaveCount{7};
    // Lattice cells across the tile at octave 0. An integer because tiling
    // needs every octave's period to be one; see the lacunarity constant.
    int m_baseFrequency{2};
    // Amplitude of each octave relative to the one before it. At 0.5 every
    // octave contributes the same amount of *slope*, since amplitude halving
    // and frequency doubling cancel, and the surface reads as fuzz. Below that
    // the coarse octaves shape the terrain and the fine ones texture it.
    double m_gain{0.45};
    // Peak radial displacement, in metres. The summed octaves are normalized
    // before this is applied, so it is the amplitude the map actually reaches.
    double m_amplitudeMetres{1.0};
    // Metres spanned by one tile. Sets the scale slopes are measured against;
    // whatever samples the baked map must lay it down at exactly this size or
    // the baked slopes describe a surface steeper or flatter than the drawn one.
    double m_tileSizeMetres{120.0};
    uint64_t m_seed{0};
};

/**
 * @brief A square, tileable height field summed from octaves of Perlin noise.
 *
 * Built once, in double, and read back either directly or quantized into
 * textures. Everything here is metric: elevations are metres of displacement,
 * slopes are metres per metre, and the caller never needs to know the texel
 * spacing to interpret either.
 *
 * Slopes are baked alongside rather than left to be differenced later because
 * a central difference taken here can use the unquantized field. They are
 * measured by finite difference rather than by differentiating the noise, so
 * they describe the map that was actually written, sampling error included.
 *
 * Deterministic: the same config always produces the same map.
 *
 * Knows nothing about GL, spheres, or how the map will be projected.
 */
class TileableHeightMap {
public:
    explicit TileableHeightMap(const TileableHeightMapConfig& config);

    const TileableHeightMapConfig& config() const { return m_config; }

    // Radial displacement at a texel, in metres. Indices wrap.
    double elevation(int x, int y) const;
    // Gradient of the elevation at a texel, in metres per metre, against the
    // map's own two axes. Indices wrap.
    glm::dvec2 slope(int x, int y) const;

    // Elevation quantized to 16-bit unorm, row major, with the affine mapping
    // back to metres it was normalized against: metres = min + unorm * range.
    // Fixed point rather than half float because the field is bounded, so
    // uniform absolute precision beats a mantissa that coarsens with altitude.
    struct ElevationBake {
        std::vector<uint16_t> m_texels;
        double m_minMetres{0.0};
        double m_rangeMetres{0.0};
    };
    ElevationBake bakeElevation() const;

    /**
     * @brief Slope quantized to a 16-bit unorm pair per texel, row major.
     *
     * Encoded as e = g / (1 + |g|) mapped from (-1, 1) onto the unorm range,
     * and decoded as g = e / (1 - |e|). The squash earns its place twice. It
     * bounds an unbounded quantity, which fixed point cannot hold otherwise;
     * and it spends resolution where it changes the normal, since the normal's
     * angle is atan(g) and so turns fastest per unit of slope near flat. A
     * uniform quantization of g would put its coarsest angular steps on exactly
     * the gentle terrain where they are most visible.
     *
     * Values are held one step clear of both endpoints, which the decode needs:
     * |e| = 1 is the slope at infinity and divides by zero.
     */
    std::vector<uint16_t> bakeSlope() const;

private:
    // Frequency ratio between one octave and the next. Not configurable: tiling
    // requires every octave's period to be a whole number of cells, and only an
    // integer ratio keeps that true all the way up from an integer base.
    static constexpr double k_lacunarity{2.0};
    // Texels per lattice cell the finest octave must have. The slopes are
    // central differences one texel wide, so they run out of resolution about
    // an octave before the elevation does, and this is a bound on the slopes
    // rather than on the field itself.
    static constexpr int k_minTexelsPerCell{8};

    TileableHeightMapConfig m_config{};
    std::vector<double> m_elevation;
    std::vector<glm::dvec2> m_slope;

    int wrapTexel(int index) const;
    int texelIndex(int x, int y) const;

    void generateElevation();
    void generateSlope();
};
