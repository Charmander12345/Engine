#pragma once

#include <cmath>
#include "../AssetManager/json.hpp"
using json = nlohmann::json;

struct Vec3
{
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
};

struct Vec2
{
    float x{0.0f};
    float y{0.0f};
};

struct Vec4
{
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float w{0.0f};
};

struct Mat3
{
    float m[9]{
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
};

struct Mat4
{
    float m[16]{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    static Mat4 transpose(const Mat4& a)
    {
        Mat4 t;
        t.m[0] = a.m[0];
        t.m[1] = a.m[4];
        t.m[2] = a.m[8];
        t.m[3] = a.m[12];

        t.m[4] = a.m[1];
        t.m[5] = a.m[5];
        t.m[6] = a.m[9];
        t.m[7] = a.m[13];

        t.m[8] = a.m[2];
        t.m[9] = a.m[6];
        t.m[10] = a.m[10];
        t.m[11] = a.m[14];

        t.m[12] = a.m[3];
        t.m[13] = a.m[7];
        t.m[14] = a.m[11];
        t.m[15] = a.m[15];
        return t;
    }
};

class Transform
{
public:
    Transform() = default;
    Transform(Vec3 position, Vec3 rotation = {}, Vec3 scale = {1.0f, 1.0f, 1.0f})
        : m_position(position), m_rotation(rotation), m_scale(scale)
    {
    }

    void setPosition(const Vec3& p) { m_position = p; }
    void setRotation(const Vec3& r) { m_rotation = r; }
    void setScale(const Vec3& s) { m_scale = s; }

    const Vec3& getPosition() const { return m_position; }
    const Vec3& getRotation() const { return m_rotation; }
    const Vec3& getScale() const { return m_scale; }

    Mat3 getScaleMat3() const
    {
        Mat3 out;
        out.m[0] = m_scale.x;
        out.m[4] = m_scale.y;
        out.m[8] = m_scale.z;
        return out;
    }

    // Rotation Mat3 from Euler angles in radians (XYZ order)
    Mat3 getRotationMat3() const
    {
        const float cx = cosf(m_rotation.x);
        const float sx = sinf(m_rotation.x);
        const float cy = cosf(m_rotation.y);
        const float sy = sinf(m_rotation.y);
        const float cz = cosf(m_rotation.z);
        const float sz = sinf(m_rotation.z);

        // Rz * Ry * Rx
        Mat3 out;
        out.m[0] = cz * cy;
        out.m[1] = cz * sy * sx - sz * cx;
        out.m[2] = cz * sy * cx + sz * sx;

        out.m[3] = sz * cy;
        out.m[4] = sz * sy * sx + cz * cx;
        out.m[5] = sz * sy * cx - cz * sx;

        out.m[6] = -sy;
        out.m[7] = cy * sx;
        out.m[8] = cy * cx;
        return out;
    }

    // Engine canonical: column-major TRS (common for math libs). Backend can transpose if needed.
    Mat4 getMatrix4ColumnMajor() const
    {
        Mat4 out;

        const Mat3 r = getRotationMat3();

        // Apply scale to rotation basis: M3 = R * S
        out.m[0] = r.m[0] * m_scale.x;
        out.m[1] = r.m[1] * m_scale.x;
        out.m[2] = r.m[2] * m_scale.x;
        out.m[3] = 0.0f;

        out.m[4] = r.m[3] * m_scale.y;
        out.m[5] = r.m[4] * m_scale.y;
        out.m[6] = r.m[5] * m_scale.y;
        out.m[7] = 0.0f;

        out.m[8] = r.m[6] * m_scale.z;
        out.m[9] = r.m[7] * m_scale.z;
        out.m[10] = r.m[8] * m_scale.z;
        out.m[11] = 0.0f;

        out.m[12] = m_position.x;
        out.m[13] = m_position.y;
        out.m[14] = m_position.z;
        out.m[15] = 1.0f;

        return out;
    }

    Mat4 getMatrix4RowMajor() const
    {
        return Mat4::transpose(getMatrix4ColumnMajor());
    }

private:
    Vec3 m_position{};
    Vec3 m_rotation{};
    Vec3 m_scale{1.0f, 1.0f, 1.0f};
};
