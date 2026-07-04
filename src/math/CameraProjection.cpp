// CameraProjection.cpp
#include "CameraProjection.h"
#include "PaniniProjection.h"
#include <cmath>

glm::dvec2 CameraProjection::worldToScreen(const glm::dvec3& worldPos,
                                          const glm::dvec3& cameraPos,
                                          const glm::dquat& cameraOri,
                                          double fov,
                                          double aspectRatio,
                                          double paniniHorizontal,
                                          double paniniVertical,
                                          double paniniFitScale) {
    // Transform to camera space
    glm::dvec3 cameraSpace = worldToCamera(worldPos, cameraPos, cameraOri);

    // Check if point is behind camera
    if (isBehindCamera(cameraSpace)) {
        // Point is behind camera, mark as non-selectable
        return glm::dvec2(-2.0, -2.0);
    }

    // Perspective projection to tan space
    glm::dvec2 projected{ cameraSpace.x / cameraSpace.y, cameraSpace.z / cameraSpace.y };

    // Apply the forward Panini distortion and undo the renderer's fit zoom so
    // anchors land where the post pass displays the same world position.
    projected = PaniniProjection::distort(projected, paniniHorizontal, paniniVertical);
    projected /= paniniFitScale;

    // Convert to normalized screen coordinates using FOV
    double tanHalfFov = std::tan(fov * 0.5);
    return projected / tanHalfFov;  // Don't adjust y for aspect ratio
}

glm::dvec3 CameraProjection::worldToCamera(const glm::dvec3& worldPos,
                                          const glm::dvec3& cameraPos,
                                          const glm::dquat& cameraOri) {
    // Transform to camera space (camera looks down +Y axis)
    return glm::conjugate(cameraOri) * (worldPos - cameraPos);
}

bool CameraProjection::isBehindCamera(const glm::dvec3& cameraSpacePos) {
    // In camera space, +Y is forward, so anything with y <= 0 is behind the camera
    return cameraSpacePos.y <= 0.0;
}