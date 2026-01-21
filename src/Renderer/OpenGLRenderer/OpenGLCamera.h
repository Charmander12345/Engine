#pragma once

#include "../Camera.h"

#include <glm/glm.hpp>

class OpenGLCamera final : public Camera
{
public:
    OpenGLCamera();

    void move(const Vec3& delta) override;
    void rotate(float yawDeltaDegrees, float pitchDeltaDegrees) override;

    Mat4 getViewMatrixColumnMajor() const override;
    const Vec3& getPosition() const override;

private:
    void clampPitch();

    Vec3 m_position{ 0.0f, 0.0f, 3.0f };
    float m_yawDeg{ -90.0f };   // looking down -Z
    float m_pitchDeg{ 0.0f };

    glm::vec3 m_front{ 0.0f, 0.0f, -1.0f };
    glm::vec3 m_up{ 0.0f, 1.0f, 0.0f };
};
