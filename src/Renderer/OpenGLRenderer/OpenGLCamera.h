#pragma once

#include "../Camera.h"

#include <glm/glm.hpp>

class OpenGLCamera final : public Camera
{
public:
    OpenGLCamera();

    void move(const Vec3& delta) override;
    void moveRelative(float forward, float right, float up) override;
    void rotate(float yawDeltaDegrees, float pitchDeltaDegrees) override;
    void setPosition(const Vec3& position) override;
    Vec2 getRotationDegrees() const override;
    void setRotationDegrees(float yawDegrees, float pitchDegrees) override;

    Mat4 getViewMatrixColumnMajor() const override;
    const Vec3& getPosition() const override;

private:
    void clampPitch();
    void updateVectors();

    Vec3 m_position{ 0.0f, 0.0f, 3.0f };
    float m_yawDeg{ -90.0f };   // looking down -Z
    float m_pitchDeg{ 0.0f };

    glm::vec3 m_front{ 0.0f, 0.0f, -1.0f };
    glm::vec3 m_up{ 0.0f, 1.0f, 0.0f };
};
