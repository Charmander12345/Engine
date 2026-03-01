#pragma once

#include "../Core/MathTypes.h"

class Camera
{
public:
    virtual ~Camera() = default;

    // World-space delta (rarely used for player-like controls)
    virtual void move(const Vec3& delta) = 0;

    // Local-space movement relative to camera orientation.
    // forward: +forward (look direction)
    // right: +right
    // up: +up
    virtual void moveRelative(float forward, float right, float up) = 0;

    // yaw: rotation around world up (Y), pitch: rotation around local right (X)
    virtual void rotate(float yawDeltaDegrees, float pitchDeltaDegrees) = 0;

    virtual void setPosition(const Vec3& position) = 0;
    virtual Vec2 getRotationDegrees() const = 0;
    virtual void setRotationDegrees(float yawDegrees, float pitchDegrees) = 0;

    virtual Mat4 getViewMatrixColumnMajor() const = 0;

    virtual const Vec3& getPosition() const = 0;
};
