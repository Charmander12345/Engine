#include "OpenGLCamera.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace
{
    constexpr float kMaxPitch = 89.0f;
}

OpenGLCamera::OpenGLCamera()
{
}

void OpenGLCamera::clampPitch()
{
    m_pitchDeg = std::clamp(m_pitchDeg, -kMaxPitch, kMaxPitch);
}

void OpenGLCamera::move(const Vec3& delta)
{
    // delta is expected in world space for now.
    m_position.x += delta.x;
    m_position.y += delta.y;
    m_position.z += delta.z;
}

void OpenGLCamera::moveRelative(float forward, float right, float up)
{
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    const glm::vec3 camRight = glm::normalize(glm::cross(m_front, worldUp));
    const glm::vec3 camUp = glm::normalize(glm::cross(camRight, m_front));

    const glm::vec3 delta = (m_front * forward) + (camRight * right) + (camUp * up);

    m_position.x += delta.x;
    m_position.y += delta.y;
    m_position.z += delta.z;
}

void OpenGLCamera::rotate(float yawDeltaDegrees, float pitchDeltaDegrees)
{
    m_yawDeg += yawDeltaDegrees;
    m_pitchDeg += pitchDeltaDegrees;
    clampPitch();

    const float yaw = glm::radians(m_yawDeg);
    const float pitch = glm::radians(m_pitchDeg);

    glm::vec3 front;
    front.x = cosf(yaw) * cosf(pitch);
    front.y = sinf(pitch);
    front.z = sinf(yaw) * cosf(pitch);
    m_front = glm::normalize(front);

    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    auto right = glm::normalize(glm::cross(m_front, worldUp));
    m_up = glm::normalize(glm::cross(right, m_front));
}

Mat4 OpenGLCamera::getViewMatrixColumnMajor() const
{
    const glm::vec3 pos(m_position.x, m_position.y, m_position.z);
    const glm::mat4 view = glm::lookAt(pos, pos + m_front, m_up);

    Mat4 out;
    const float* src = glm::value_ptr(view);
    for (int i = 0; i < 16; ++i)
    {
        out.m[i] = src[i];
    }
    return out;
}

const Vec3& OpenGLCamera::getPosition() const
{
    return m_position;
}
