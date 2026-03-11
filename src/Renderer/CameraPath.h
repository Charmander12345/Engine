#pragma once

#include "../Core/MathTypes.h"
#include <vector>
#include <cmath>

/// A single control point on a cinematic camera path.
struct CameraPathPoint
{
    Vec3  position{};
    float yaw{0.0f};    // degrees
    float pitch{0.0f};  // degrees
};

/// Catmull-Rom spline evaluation for a sequence of control points.
/// The path parameter `t` ranges from 0 (first point) to 1 (last point).
struct CameraPath
{
    std::vector<CameraPathPoint> points;
    float duration{1.0f};   // total playback time in seconds
    bool  loop{false};

    bool isValid() const { return points.size() >= 2; }

    /// Evaluate the Catmull-Rom spline at normalised parameter `t` ∈ [0,1].
    CameraPathPoint evaluate(float t) const
    {
        if (points.empty())
            return {};
        if (points.size() == 1)
            return points[0];

        // Clamp or wrap t
        if (loop)
        {
            t = t - std::floor(t);
            if (t < 0.0f) t += 1.0f;
        }
        else
        {
            if (t <= 0.0f) return points.front();
            if (t >= 1.0f) return points.back();
        }

        const int n = static_cast<int>(points.size());
        const float segment = t * static_cast<float>(n - 1);
        int i = static_cast<int>(std::floor(segment));
        float local = segment - static_cast<float>(i);

        // Clamp index to valid range
        if (i >= n - 1) { i = n - 2; local = 1.0f; }

        // Four control points for Catmull-Rom: P0, P1, P2, P3
        auto idx = [&](int k) -> int {
            if (loop) return ((k % n) + n) % n;
            if (k < 0) return 0;
            if (k >= n) return n - 1;
            return k;
        };

        const CameraPathPoint& p0 = points[idx(i - 1)];
        const CameraPathPoint& p1 = points[idx(i)];
        const CameraPathPoint& p2 = points[idx(i + 1)];
        const CameraPathPoint& p3 = points[idx(i + 2)];

        // Catmull-Rom interpolation: q(t) = 0.5 * ((2P1) + (-P0+P2)*t + (2P0-5P1+4P2-P3)*t² + (-P0+3P1-3P2+P3)*t³)
        auto catmull = [](float a, float b, float c, float d, float u) -> float {
            return 0.5f * ((2.0f * b) +
                           (-a + c) * u +
                           (2.0f * a - 5.0f * b + 4.0f * c - d) * u * u +
                           (-a + 3.0f * b - 3.0f * c + d) * u * u * u);
        };

        CameraPathPoint result;
        result.position.x = catmull(p0.position.x, p1.position.x, p2.position.x, p3.position.x, local);
        result.position.y = catmull(p0.position.y, p1.position.y, p2.position.y, p3.position.y, local);
        result.position.z = catmull(p0.position.z, p1.position.z, p2.position.z, p3.position.z, local);
        result.yaw        = catmull(p0.yaw,        p1.yaw,        p2.yaw,        p3.yaw,        local);
        result.pitch      = catmull(p0.pitch,      p1.pitch,      p2.pitch,      p3.pitch,      local);
        return result;
    }
};
