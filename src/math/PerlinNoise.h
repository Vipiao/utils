// PerlinNoise.h
#pragma once

#include <cstdint>
#include <glm/glm.hpp>

/**
 * @brief One layer of periodic Perlin gradient noise, in two or three axes.
 *
 * The lattice is a square grid of unit gradient vectors, one per cell corner,
 * and a sample is the faded bilinear blend of the four corners' dot products
 * against the offsets to them -- standard Perlin.
 *
 * What makes it tileable is that the corner lookup wraps: a cell index outside
 * [0, period) is taken modulo the period, so the lattice a sample sees at x and
 * at x + period is the same one. The field is then exactly periodic with that
 * period in both axes, with no seam to blend over.
 *
 * The gradients are not stored. Each is hashed from its wrapped cell index and
 * the seed on demand, which is the same lattice a table would hold, without the
 * table. Deterministic: the same cell and seed always give the same vector.
 */
class PerlinNoise {
public:
    // Sample at a point in lattice units -- one unit is one cell, so a tile of
    // `period` cells spans [0, period). Points outside it wrap. The result is
    // roughly [-1, 1] and is exactly 0 at every lattice corner.
    static double sample(const glm::dvec2& point, int64_t period, uint64_t seed);

    // The same field over three axes, returning the field's gradient alongside
    // its value. One entry point rather than two because the gradient is nearly
    // free taken with the value and several times the cost taken apart from it,
    // and a caller baking a map wants both at every texel.
    static double sample(const glm::dvec3& point, int64_t period, uint64_t seed,
                         glm::dvec3& gradient);

private:
    // Perlin's quintic fade, whose first and second derivatives vanish at 0 and
    // 1. The second derivative is what matters here: with only a cubic the
    // curvature steps across cell boundaries, and the gradients this feeds are
    // visibly creased along the lattice.
    static double fade(double t);
    // Its slope, which the gradient carries through the blend.
    static double fadeSlope(double t);

    // Unit gradient of one lattice corner, indexed by its wrapped cell.
    static glm::dvec2 cellGradient(const glm::i64vec2& cell, uint64_t seed);
    static glm::dvec3 cellGradient(const glm::i64vec3& cell, uint64_t seed);

    // Non-negative remainder, so cells left of and below the tile wrap onto it
    // rather than reflecting off zero the way a truncating % would.
    static int64_t wrapCell(int64_t index, int64_t period);
};
