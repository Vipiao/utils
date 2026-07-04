// BlueNoise.h
#pragma once

#include <cstdint>
#include <vector>

/**
 * @brief Blue noise threshold map generation (Ulichney's void-and-cluster).
 *
 * Produces a square, tileable map where each texel holds its rank in a
 * dither ordering, scaled to [0, 255]. Ranks are placed so that every
 * threshold slice of the map is spatially homogeneous, which concentrates
 * the noise energy in high frequencies (a "blue" spectrum). Used to
 * decorrelate quantization error when a continuous color is written to a
 * low-bit-depth target.
 *
 * Deterministic: the same size and seed always produce the same map.
 */
class BlueNoise {
public:
    // Tileable size x size map; each texel is its rank scaled to [0, 255].
    // Cost is O(size^4); intended for small maps (e.g. 64) built at startup.
    static std::vector<uint8_t> generate(int size, uint64_t seed = 0);
};
