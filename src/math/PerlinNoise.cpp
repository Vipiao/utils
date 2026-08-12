// PerlinNoise.cpp
#include "PerlinNoise.h"
#include "utils/HashFunctions.h"
#include <cassert>
#include <cmath>

// Largest magnitude 2D Perlin can reach with unit gradients is sqrt(2) / 2, at
// a cell centre with all four gradients pointing at it. Scaling by its
// reciprocal puts the field in [-1, 1] so callers can weight octaves directly.
static constexpr double s_rangeScale{1.4142135623730951};

double PerlinNoise::fade(double t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

int64_t PerlinNoise::wrapCell(int64_t index, int64_t period) {
    const int64_t remainder{index % period};
    return remainder < 0 ? remainder + period : remainder;
}

glm::dvec2 PerlinNoise::cellGradient(const glm::i64vec2& cell, uint64_t seed) {
    // An angle rather than a rejection-sampled vector: every gradient is unit
    // length by construction, which is what the range scale above assumes.
    const double angle{Hash::pcgUnit(glm::i64vec3{cell.x, cell.y,
                                                 static_cast<int64_t>(seed)}) *
                       6.283185307179586};
    return glm::dvec2{std::cos(angle), std::sin(angle)};
}

double PerlinNoise::sample(const glm::dvec2& point, int64_t period, uint64_t seed) {
    assert(period > 0 && "A lattice with no cells has no gradients to interpolate");

    const glm::dvec2 cellOrigin{glm::floor(point)};
    const glm::dvec2 offset{point - cellOrigin};
    const glm::i64vec2 baseCell{static_cast<int64_t>(cellOrigin.x),
                                static_cast<int64_t>(cellOrigin.y)};

    // Dot of each corner's gradient with the offset from that corner to the
    // sample. Corner order is (0,0), (1,0), (0,1), (1,1).
    double cornerDots[4]{};
    for (int cornerIndex{0}; cornerIndex < 4; ++cornerIndex) {
        const glm::i64vec2 corner{cornerIndex % 2, cornerIndex / 2};
        const glm::i64vec2 cell{wrapCell(baseCell.x + corner.x, period),
                                wrapCell(baseCell.y + corner.y, period)};
        const glm::dvec2 toSample{offset - glm::dvec2{corner}};
        cornerDots[cornerIndex] = glm::dot(cellGradient(cell, seed), toSample);
    }

    const double uFade{fade(offset.x)};
    const double vFade{fade(offset.y)};
    const double lowerEdge{glm::mix(cornerDots[0], cornerDots[1], uFade)};
    const double upperEdge{glm::mix(cornerDots[2], cornerDots[3], uFade)};

    return glm::mix(lowerEdge, upperEdge, vFade) * s_rangeScale;
}
