// PerlinNoise.cpp
#include "PerlinNoise.h"
#include "utils/HashFunctions.h"
#include <cassert>
#include <cmath>

// Largest magnitude 2D Perlin can reach with unit gradients is sqrt(2) / 2, at
// a cell centre with all four gradients pointing at it. Scaling by its
// reciprocal puts the field in [-1, 1] so callers can weight octaves directly.
static constexpr double s_rangeScale{1.4142135623730951};

// The same bound one axis further out: sqrt(3) / 2, so this is its reciprocal.
static constexpr double s_rangeScale3{1.1547005383792515};

// The twelve midpoints of a cube's edges, at unit length. Perlin's own set for
// three axes, drawn from a table rather than from hashed angles as the two-axis
// gradients are: a sphere has no parameterization as cheap as the circle's
// cosine and sine, and this runs eight times an octave for every texel of a
// baked map. Spread evenly enough that no direction is favoured, and every one
// of them is unit length, which is what the range scale above assumes.
static constexpr double s_edge{0.7071067811865476};
static const glm::dvec3 s_edgeGradients[12]{
    {s_edge, s_edge, 0.0},  {-s_edge, s_edge, 0.0},  {s_edge, -s_edge, 0.0},
    {-s_edge, -s_edge, 0.0}, {s_edge, 0.0, s_edge},  {-s_edge, 0.0, s_edge},
    {s_edge, 0.0, -s_edge}, {-s_edge, 0.0, -s_edge}, {0.0, s_edge, s_edge},
    {0.0, -s_edge, s_edge}, {0.0, s_edge, -s_edge},  {0.0, -s_edge, -s_edge}};

double PerlinNoise::fade(double t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

double PerlinNoise::fadeSlope(double t) {
    const double leftover{t - 1.0};
    return 30.0 * t * t * leftover * leftover;
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

glm::dvec3 PerlinNoise::cellGradient(const glm::i64vec3& cell, uint64_t seed) {
    return s_edgeGradients[Hash::pcg(Hash::pcg(cell), seed) % 12u];
}

double PerlinNoise::sample(const glm::dvec3& point, int64_t period, uint64_t seed,
                           glm::dvec3& gradient) {
    assert(period > 0 && "A lattice with no cells has no gradients to interpolate");

    const glm::dvec3 cellOrigin{glm::floor(point)};
    const glm::dvec3 offset{point - cellOrigin};
    const glm::i64vec3 baseCell{static_cast<int64_t>(cellOrigin.x),
                                static_cast<int64_t>(cellOrigin.y),
                                static_cast<int64_t>(cellOrigin.z)};

    // How far the blend has travelled across the cell along each axis, and how
    // fast it is travelling there.
    glm::dvec3 blend{};
    glm::dvec3 blendSlope{};
    for (int axis{0}; axis < 3; ++axis) {
        blend[axis] = fade(offset[axis]);
        blendSlope[axis] = fadeSlope(offset[axis]);
    }

    double value{0.0};
    glm::dvec3 slope{0.0};
    for (int cornerIndex{0}; cornerIndex < 8; ++cornerIndex) {
        const glm::i64vec3 corner{cornerIndex & 1, (cornerIndex >> 1) & 1,
                                  (cornerIndex >> 2) & 1};
        const glm::i64vec3 cell{wrapCell(baseCell.x + corner.x, period),
                                wrapCell(baseCell.y + corner.y, period),
                                wrapCell(baseCell.z + corner.z, period)};
        const glm::dvec3 cornerGradient{cellGradient(cell, seed)};
        const glm::dvec3 toSample{offset - glm::dvec3{corner}};
        const double cornerDot{glm::dot(cornerGradient, toSample)};

        // This corner's share of the blend is one factor per axis, the corner
        // taking either end of that axis' travel.
        glm::dvec3 share{};
        for (int axis{0}; axis < 3; ++axis) {
            share[axis] = corner[axis] == 1 ? blend[axis] : 1.0 - blend[axis];
        }
        const double weight{share.x * share.y * share.z};
        value += cornerDot * weight;

        // Two terms per axis: the corner's own gradient carried by its share,
        // and the dot product carried by how that share changes. The share's
        // slope swaps one axis' factor for the factor's own slope, which is why
        // the other two are multiplied out rather than divided back out -- a
        // share falls to zero at the cell's own corners.
        for (int axis{0}; axis < 3; ++axis) {
            const double travel{corner[axis] == 1 ? blendSlope[axis] : -blendSlope[axis]};
            const double otherAxes{share[(axis + 1) % 3] * share[(axis + 2) % 3]};
            slope[axis] += cornerGradient[axis] * weight + cornerDot * travel * otherAxes;
        }
    }

    gradient = slope * s_rangeScale3;
    return value * s_rangeScale3;
}
