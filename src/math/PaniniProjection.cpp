// PaniniProjection.cpp
#include "PaniniProjection.h"
#include <algorithm>
#include <cmath>

// Forward scale factor of one cylindrical pass. With phi = atan(coordinate):
// (d + 1) * cos(phi) / (d + cos(phi)), which reduces to the sqrt form below.
// Exact identity when strength is 0.
double PaniniProjection::passScale(double coordinate, double strength) {
    return (strength + 1.0) /
           (strength * std::sqrt(1.0 + coordinate * coordinate) + 1.0);
}

glm::dvec2 PaniniProjection::distort(const glm::dvec2& tanCoord,
                                     double horizontal, double vertical) {
    glm::dvec2 t{ tanCoord };
    t *= passScale(t.x, horizontal);
    t *= passScale(t.y, vertical);
    return t;
}

// Inverse of one cylindrical pass acting on the x axis. Mirrors the post shader.
glm::dvec2 PaniniProjection::inversePass(const glm::dvec2& t, double strength) {
    double d = strength;
    double x = t.x;
    double r = std::sqrt((d + 1.0) * (d + 1.0) + x * x);
    double phi = std::asin(std::clamp(x * d / r, -1.0, 1.0)) + std::atan2(x, d + 1.0);
    double tanPhi = std::tan(phi);
    // The forward pass scales both axes by passScale (evaluated at the
    // rectilinear coordinate, which is tan(phi) here); undo it on y.
    return { tanPhi, t.y / passScale(tanPhi, d) };
}

glm::dvec2 PaniniProjection::undistort(const glm::dvec2& tanCoord,
                                       double horizontal, double vertical) {
    // The forward order is horizontal then vertical, so invert in reverse order.
    glm::dvec2 t = inversePass({ tanCoord.y, tanCoord.x }, vertical);
    t = { t.y, t.x };
    return inversePass(t, horizontal);
}

double PaniniProjection::fitScale(double horizontal, double vertical,
                                  const glm::dvec2& tanEdge) {
    if (horizontal <= 0.0 && vertical <= 0.0) {
        return 1.0;
    }
    // Bisection. The screen corner is the binding constraint: both passes grow
    // monotonically with each coordinate, so if the corner fits, all edges fit.
    double low = 0.0;  // Always fits.
    double high = 1.0; // The unscaled mapping overflows for any positive strength.
    for (int i = 0; i < 60; ++i) {
        double mid = 0.5 * (low + high);
        glm::dvec2 source = undistort(mid * tanEdge, horizontal, vertical);
        bool inside = std::abs(source.x) <= tanEdge.x &&
                      std::abs(source.y) <= tanEdge.y;
        if (inside) {
            low = mid;
        } else {
            high = mid;
        }
    }
    // Small margin so float rounding in the shader cannot push border pixels
    // outside the source and trigger the discard there.
    return low * (1.0 - 1e-5);
}
