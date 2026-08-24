// Cylinder.h
#pragma once

#include <glm/glm.hpp>

/**
 * @brief A circular cross-section swept along an axis.
 *
 * Named for the solid rather than for anything that bounds itself with one, so
 * whatever wants a swept round volume can measure against it. The extent along
 * the axis is signed and measured from the centre, which lets the volume reach
 * further one way than the other without moving the centre off what it describes.
 */
struct Cylinder {
    glm::dvec3 m_axis{0.0, 0.0, 1.0};  // unit length
    glm::dvec3 m_centre{0.0};
    double m_radius{0.0};
    double m_axialMin{0.0};
    double m_axialMax{0.0};
};

/**
 * @brief Whether a sphere reaches any part of a cylinder.
 *
 * The two extents are tested apart, so a sphere off one rim counts as inside
 * both and is kept. Culling wants exactly that: what it costs is a volume that
 * was going to be drawn anyway, where the other error drops one that was not.
 */
inline bool intersectsSphere(const Cylinder& cylinder, const glm::dvec3& centre,
                             double radius) {
    const glm::dvec3 offset{centre - cylinder.m_centre};

    const double along{glm::dot(offset, cylinder.m_axis)};
    if (along < cylinder.m_axialMin - radius || along > cylinder.m_axialMax + radius) {
        return false;
    }

    const glm::dvec3 perpendicular{offset - cylinder.m_axis * along};
    const double reach{cylinder.m_radius + radius};
    return glm::dot(perpendicular, perpendicular) <= reach * reach;
}
