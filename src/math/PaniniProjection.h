// PaniniProjection.h
#pragma once

#include <glm/glm.hpp>

/**
 * @brief Cylindrical Panini projection distortion in tan space.
 *
 * Tan space is view-space xy at unit forward distance. The forward mapping is
 * a horizontal cylinder pass followed by a vertical one; strengths are in
 * [0, 1] where 0 is rectilinear (exact identity) and 1 is max distortion.
 *
 * The renderer applies the inverse mapping when resampling the finished frame,
 * pre-scaled by fitScale() so the distorted image fills the screen using only
 * rendered data. Screen anchors must therefore be placed at
 * distort(tanCoord) / fitScale(...) to line up with the displayed image.
 */
class PaniniProjection {
public:
    // Forward distortion of a rectilinear tan-space coordinate.
    static glm::dvec2 distort(const glm::dvec2& tanCoord,
                              double horizontal, double vertical);

    // Inverse mapping: the rectilinear tan-space source of a distorted coordinate.
    static glm::dvec2 undistort(const glm::dvec2& tanCoord,
                                double horizontal, double vertical);

    /**
     * @brief Largest output pre-scale that keeps every undistorted source
     *        lookup inside the rendered frustum.
     * @param tanEdge Frustum half extents in tan space:
     *        (tan(horizontalFov / 2), tan(verticalFov / 2))
     */
    static double fitScale(double horizontal, double vertical,
                           const glm::dvec2& tanEdge);

private:
    static double passScale(double coordinate, double strength);
    static glm::dvec2 inversePass(const glm::dvec2& t, double strength);
};
