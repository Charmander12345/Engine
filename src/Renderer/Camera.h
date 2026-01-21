#pragma once

#include "../Basics/MathTypes.h"

class Camera
{
public:
    virtual ~Camera() = default;

    virtual void move(const Vec3& delta) = 0;

    // yaw: rotation around world up (Y), pitch: rotation around local right (X)
    virtual void rotate(float yawDeltaDegrees, float pitchDeltaDegrees) = 0;

    virtual Mat4 getViewMatrixColumnMajor() const = 0;

    virtual const Vec3& getPosition() const = 0;
};
