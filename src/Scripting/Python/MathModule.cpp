#include "ScriptingInternal.h"

#include <cmath>
#include <algorithm>

namespace
{
    // ── engine.math ─────────────────────────────────────────────────────

    // --- Vec3 ---

    PyObject* py_vec3(PyObject*, PyObject* args)
    {
        float x = 0, y = 0, z = 0;
        if (!PyArg_ParseTuple(args, "|fff", &x, &y, &z)) return nullptr;
        return Py_BuildValue("(fff)", x, y, z);
    }

    PyObject* py_vec3_add(PyObject*, PyObject* args)
    {
        float ax, ay, az, bx, by, bz;
        if (!PyArg_ParseTuple(args, "(fff)(fff)", &ax, &ay, &az, &bx, &by, &bz)) return nullptr;
        return Py_BuildValue("(fff)", ax + bx, ay + by, az + bz);
    }

    PyObject* py_vec3_sub(PyObject*, PyObject* args)
    {
        float ax, ay, az, bx, by, bz;
        if (!PyArg_ParseTuple(args, "(fff)(fff)", &ax, &ay, &az, &bx, &by, &bz)) return nullptr;
        return Py_BuildValue("(fff)", ax - bx, ay - by, az - bz);
    }

    PyObject* py_vec3_mul(PyObject*, PyObject* args)
    {
        float ax, ay, az, bx, by, bz;
        if (!PyArg_ParseTuple(args, "(fff)(fff)", &ax, &ay, &az, &bx, &by, &bz)) return nullptr;
        return Py_BuildValue("(fff)", ax * bx, ay * by, az * bz);
    }

    PyObject* py_vec3_div(PyObject*, PyObject* args)
    {
        float ax, ay, az, bx, by, bz;
        if (!PyArg_ParseTuple(args, "(fff)(fff)", &ax, &ay, &az, &bx, &by, &bz)) return nullptr;
        if (std::abs(bx) < 1e-8f || std::abs(by) < 1e-8f || std::abs(bz) < 1e-8f)
        { PyErr_SetString(PyExc_ZeroDivisionError, "Division by zero in vec3_div."); return nullptr; }
        return Py_BuildValue("(fff)", ax / bx, ay / by, az / bz);
    }

    PyObject* py_vec3_scale(PyObject*, PyObject* args)
    {
        float x, y, z, s;
        if (!PyArg_ParseTuple(args, "(fff)f", &x, &y, &z, &s)) return nullptr;
        return Py_BuildValue("(fff)", x * s, y * s, z * s);
    }

    PyObject* py_vec3_dot(PyObject*, PyObject* args)
    {
        float ax, ay, az, bx, by, bz;
        if (!PyArg_ParseTuple(args, "(fff)(fff)", &ax, &ay, &az, &bx, &by, &bz)) return nullptr;
        return PyFloat_FromDouble(static_cast<double>(ax * bx + ay * by + az * bz));
    }

    PyObject* py_vec3_cross(PyObject*, PyObject* args)
    {
        float ax, ay, az, bx, by, bz;
        if (!PyArg_ParseTuple(args, "(fff)(fff)", &ax, &ay, &az, &bx, &by, &bz)) return nullptr;
        return Py_BuildValue("(fff)", ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx);
    }

    PyObject* py_vec3_length(PyObject*, PyObject* args)
    {
        float x, y, z;
        if (!PyArg_ParseTuple(args, "(fff)", &x, &y, &z)) return nullptr;
        return PyFloat_FromDouble(std::sqrt(static_cast<double>(x * x + y * y + z * z)));
    }

    PyObject* py_vec3_length_sq(PyObject*, PyObject* args)
    {
        float x, y, z;
        if (!PyArg_ParseTuple(args, "(fff)", &x, &y, &z)) return nullptr;
        return PyFloat_FromDouble(static_cast<double>(x * x + y * y + z * z));
    }

    PyObject* py_vec3_normalize(PyObject*, PyObject* args)
    {
        float x, y, z;
        if (!PyArg_ParseTuple(args, "(fff)", &x, &y, &z)) return nullptr;
        float len = std::sqrt(x * x + y * y + z * z);
        if (len < 1e-8f) return Py_BuildValue("(fff)", 0.0f, 0.0f, 0.0f);
        return Py_BuildValue("(fff)", x / len, y / len, z / len);
    }

    PyObject* py_vec3_negate(PyObject*, PyObject* args)
    {
        float x, y, z;
        if (!PyArg_ParseTuple(args, "(fff)", &x, &y, &z)) return nullptr;
        return Py_BuildValue("(fff)", -x, -y, -z);
    }

    PyObject* py_vec3_lerp(PyObject*, PyObject* args)
    {
        float ax, ay, az, bx, by, bz, t;
        if (!PyArg_ParseTuple(args, "(fff)(fff)f", &ax, &ay, &az, &bx, &by, &bz, &t)) return nullptr;
        return Py_BuildValue("(fff)", ax + (bx - ax) * t, ay + (by - ay) * t, az + (bz - az) * t);
    }

    PyObject* py_vec3_distance(PyObject*, PyObject* args)
    {
        float ax, ay, az, bx, by, bz;
        if (!PyArg_ParseTuple(args, "(fff)(fff)", &ax, &ay, &az, &bx, &by, &bz)) return nullptr;
        float dx = bx - ax, dy = by - ay, dz = bz - az;
        return PyFloat_FromDouble(std::sqrt(static_cast<double>(dx * dx + dy * dy + dz * dz)));
    }

    PyObject* py_vec3_reflect(PyObject*, PyObject* args)
    {
        float vx, vy, vz, nx, ny, nz;
        if (!PyArg_ParseTuple(args, "(fff)(fff)", &vx, &vy, &vz, &nx, &ny, &nz)) return nullptr;
        float d = 2.0f * (vx * nx + vy * ny + vz * nz);
        return Py_BuildValue("(fff)", vx - d * nx, vy - d * ny, vz - d * nz);
    }

    PyObject* py_vec3_min(PyObject*, PyObject* args)
    {
        float ax, ay, az, bx, by, bz;
        if (!PyArg_ParseTuple(args, "(fff)(fff)", &ax, &ay, &az, &bx, &by, &bz)) return nullptr;
        return Py_BuildValue("(fff)", std::min(ax, bx), std::min(ay, by), std::min(az, bz));
    }

    PyObject* py_vec3_max(PyObject*, PyObject* args)
    {
        float ax, ay, az, bx, by, bz;
        if (!PyArg_ParseTuple(args, "(fff)(fff)", &ax, &ay, &az, &bx, &by, &bz)) return nullptr;
        return Py_BuildValue("(fff)", std::max(ax, bx), std::max(ay, by), std::max(az, bz));
    }

    // --- Vec2 ---

    PyObject* py_vec2(PyObject*, PyObject* args)
    {
        float x = 0, y = 0;
        if (!PyArg_ParseTuple(args, "|ff", &x, &y)) return nullptr;
        return Py_BuildValue("(ff)", x, y);
    }

    PyObject* py_vec2_add(PyObject*, PyObject* args)
    {
        float ax, ay, bx, by;
        if (!PyArg_ParseTuple(args, "(ff)(ff)", &ax, &ay, &bx, &by)) return nullptr;
        return Py_BuildValue("(ff)", ax + bx, ay + by);
    }

    PyObject* py_vec2_sub(PyObject*, PyObject* args)
    {
        float ax, ay, bx, by;
        if (!PyArg_ParseTuple(args, "(ff)(ff)", &ax, &ay, &bx, &by)) return nullptr;
        return Py_BuildValue("(ff)", ax - bx, ay - by);
    }

    PyObject* py_vec2_scale(PyObject*, PyObject* args)
    {
        float x, y, s;
        if (!PyArg_ParseTuple(args, "(ff)f", &x, &y, &s)) return nullptr;
        return Py_BuildValue("(ff)", x * s, y * s);
    }

    PyObject* py_vec2_dot(PyObject*, PyObject* args)
    {
        float ax, ay, bx, by;
        if (!PyArg_ParseTuple(args, "(ff)(ff)", &ax, &ay, &bx, &by)) return nullptr;
        return PyFloat_FromDouble(static_cast<double>(ax * bx + ay * by));
    }

    PyObject* py_vec2_length(PyObject*, PyObject* args)
    {
        float x, y;
        if (!PyArg_ParseTuple(args, "(ff)", &x, &y)) return nullptr;
        return PyFloat_FromDouble(std::sqrt(static_cast<double>(x * x + y * y)));
    }

    PyObject* py_vec2_normalize(PyObject*, PyObject* args)
    {
        float x, y;
        if (!PyArg_ParseTuple(args, "(ff)", &x, &y)) return nullptr;
        float len = std::sqrt(x * x + y * y);
        if (len < 1e-8f) return Py_BuildValue("(ff)", 0.0f, 0.0f);
        return Py_BuildValue("(ff)", x / len, y / len);
    }

    PyObject* py_vec2_lerp(PyObject*, PyObject* args)
    {
        float ax, ay, bx, by, t;
        if (!PyArg_ParseTuple(args, "(ff)(ff)f", &ax, &ay, &bx, &by, &t)) return nullptr;
        return Py_BuildValue("(ff)", ax + (bx - ax) * t, ay + (by - ay) * t);
    }

    PyObject* py_vec2_distance(PyObject*, PyObject* args)
    {
        float ax, ay, bx, by;
        if (!PyArg_ParseTuple(args, "(ff)(ff)", &ax, &ay, &bx, &by)) return nullptr;
        float dx = bx - ax, dy = by - ay;
        return PyFloat_FromDouble(std::sqrt(static_cast<double>(dx * dx + dy * dy)));
    }

    // --- Quaternion (stored as tuple (x, y, z, w)) ---

    PyObject* py_quat_from_euler(PyObject*, PyObject* args)
    {
        float pitch, yaw, roll;
        if (!PyArg_ParseTuple(args, "fff", &pitch, &yaw, &roll)) return nullptr;
        const float hp = pitch * 0.5f, hy = yaw * 0.5f, hr = roll * 0.5f;
        const float cp = std::cos(hp), sp = std::sin(hp);
        const float cy = std::cos(hy), sy = std::sin(hy);
        const float cr = std::cos(hr), sr = std::sin(hr);
        float qx = sr * cp * cy - cr * sp * sy;
        float qy = cr * sp * cy + sr * cp * sy;
        float qz = cr * cp * sy - sr * sp * cy;
        float qw = cr * cp * cy + sr * sp * sy;
        return Py_BuildValue("(ffff)", qx, qy, qz, qw);
    }

    PyObject* py_quat_to_euler(PyObject*, PyObject* args)
    {
        float qx, qy, qz, qw;
        if (!PyArg_ParseTuple(args, "(ffff)", &qx, &qy, &qz, &qw)) return nullptr;
        // Roll (x)
        float sinr = 2.0f * (qw * qx + qy * qz);
        float cosr = 1.0f - 2.0f * (qx * qx + qy * qy);
        float roll = std::atan2(sinr, cosr);
        // Pitch (y)
        float sinp = 2.0f * (qw * qy - qz * qx);
        float pitch = (std::abs(sinp) >= 1.0f) ? std::copysign(3.14159265f / 2.0f, sinp) : std::asin(sinp);
        // Yaw (z)
        float siny = 2.0f * (qw * qz + qx * qy);
        float cosy = 1.0f - 2.0f * (qy * qy + qz * qz);
        float yaw = std::atan2(siny, cosy);
        return Py_BuildValue("(fff)", pitch, yaw, roll);
    }

    PyObject* py_quat_multiply(PyObject*, PyObject* args)
    {
        float ax, ay, az, aw, bx, by, bz, bw;
        if (!PyArg_ParseTuple(args, "(ffff)(ffff)", &ax, &ay, &az, &aw, &bx, &by, &bz, &bw)) return nullptr;
        float rx = aw * bx + ax * bw + ay * bz - az * by;
        float ry = aw * by - ax * bz + ay * bw + az * bx;
        float rz = aw * bz + ax * by - ay * bx + az * bw;
        float rw = aw * bw - ax * bx - ay * by - az * bz;
        return Py_BuildValue("(ffff)", rx, ry, rz, rw);
    }

    PyObject* py_quat_normalize(PyObject*, PyObject* args)
    {
        float x, y, z, w;
        if (!PyArg_ParseTuple(args, "(ffff)", &x, &y, &z, &w)) return nullptr;
        float len = std::sqrt(x * x + y * y + z * z + w * w);
        if (len < 1e-8f) return Py_BuildValue("(ffff)", 0.0f, 0.0f, 0.0f, 1.0f);
        return Py_BuildValue("(ffff)", x / len, y / len, z / len, w / len);
    }

    PyObject* py_quat_slerp(PyObject*, PyObject* args)
    {
        float ax, ay, az, aw, bx, by, bz, bw, t;
        if (!PyArg_ParseTuple(args, "(ffff)(ffff)f", &ax, &ay, &az, &aw, &bx, &by, &bz, &bw, &t)) return nullptr;
        float dot = ax * bx + ay * by + az * bz + aw * bw;
        if (dot < 0.0f) { bx = -bx; by = -by; bz = -bz; bw = -bw; dot = -dot; }
        if (dot > 0.9995f)
        {
            float rx = ax + t * (bx - ax), ry = ay + t * (by - ay);
            float rz = az + t * (bz - az), rw = aw + t * (bw - aw);
            float len = std::sqrt(rx * rx + ry * ry + rz * rz + rw * rw);
            if (len > 1e-8f) { rx /= len; ry /= len; rz /= len; rw /= len; }
            return Py_BuildValue("(ffff)", rx, ry, rz, rw);
        }
        float theta0 = std::acos(dot);
        float theta = theta0 * t;
        float sinT = std::sin(theta), sinT0 = std::sin(theta0);
        float s0 = std::cos(theta) - dot * sinT / sinT0;
        float s1 = sinT / sinT0;
        return Py_BuildValue("(ffff)", s0 * ax + s1 * bx, s0 * ay + s1 * by, s0 * az + s1 * bz, s0 * aw + s1 * bw);
    }

    PyObject* py_quat_inverse(PyObject*, PyObject* args)
    {
        float x, y, z, w;
        if (!PyArg_ParseTuple(args, "(ffff)", &x, &y, &z, &w)) return nullptr;
        float lenSq = x * x + y * y + z * z + w * w;
        if (lenSq < 1e-12f) return Py_BuildValue("(ffff)", 0.0f, 0.0f, 0.0f, 1.0f);
        float inv = 1.0f / lenSq;
        return Py_BuildValue("(ffff)", -x * inv, -y * inv, -z * inv, w * inv);
    }

    PyObject* py_quat_rotate_vec3(PyObject*, PyObject* args)
    {
        float qx, qy, qz, qw, vx, vy, vz;
        if (!PyArg_ParseTuple(args, "(ffff)(fff)", &qx, &qy, &qz, &qw, &vx, &vy, &vz)) return nullptr;
        // q * v * q^-1 optimised
        float tx = 2.0f * (qy * vz - qz * vy);
        float ty = 2.0f * (qz * vx - qx * vz);
        float tz = 2.0f * (qx * vy - qy * vx);
        float rx = vx + qw * tx + (qy * tz - qz * ty);
        float ry = vy + qw * ty + (qz * tx - qx * tz);
        float rz = vz + qw * tz + (qx * ty - qy * tx);
        return Py_BuildValue("(fff)", rx, ry, rz);
    }

    // --- Scalar utilities ---

    PyObject* py_clamp(PyObject*, PyObject* args)
    {
        float v, lo, hi;
        if (!PyArg_ParseTuple(args, "fff", &v, &lo, &hi)) return nullptr;
        return PyFloat_FromDouble(static_cast<double>(std::clamp(v, lo, hi)));
    }

    PyObject* py_lerp_scalar(PyObject*, PyObject* args)
    {
        float a, b, t;
        if (!PyArg_ParseTuple(args, "fff", &a, &b, &t)) return nullptr;
        return PyFloat_FromDouble(static_cast<double>(a + (b - a) * t));
    }

    PyObject* py_deg_to_rad(PyObject*, PyObject* args)
    {
        float d;
        if (!PyArg_ParseTuple(args, "f", &d)) return nullptr;
        return PyFloat_FromDouble(static_cast<double>(d * (3.14159265358979323846f / 180.0f)));
    }

    PyObject* py_rad_to_deg(PyObject*, PyObject* args)
    {
        float r;
        if (!PyArg_ParseTuple(args, "f", &r)) return nullptr;
        return PyFloat_FromDouble(static_cast<double>(r * (180.0f / 3.14159265358979323846f)));
    }

    // --- Trigonometric & common math ---

    PyObject* py_sin(PyObject*, PyObject* args)
    {
        float v;
        if (!PyArg_ParseTuple(args, "f", &v)) return nullptr;
        return PyFloat_FromDouble(std::sin(static_cast<double>(v)));
    }

    PyObject* py_cos(PyObject*, PyObject* args)
    {
        float v;
        if (!PyArg_ParseTuple(args, "f", &v)) return nullptr;
        return PyFloat_FromDouble(std::cos(static_cast<double>(v)));
    }

    PyObject* py_tan(PyObject*, PyObject* args)
    {
        float v;
        if (!PyArg_ParseTuple(args, "f", &v)) return nullptr;
        return PyFloat_FromDouble(std::tan(static_cast<double>(v)));
    }

    PyObject* py_asin(PyObject*, PyObject* args)
    {
        float v;
        if (!PyArg_ParseTuple(args, "f", &v)) return nullptr;
        return PyFloat_FromDouble(std::asin(static_cast<double>(v)));
    }

    PyObject* py_acos(PyObject*, PyObject* args)
    {
        float v;
        if (!PyArg_ParseTuple(args, "f", &v)) return nullptr;
        return PyFloat_FromDouble(std::acos(static_cast<double>(v)));
    }

    PyObject* py_atan(PyObject*, PyObject* args)
    {
        float v;
        if (!PyArg_ParseTuple(args, "f", &v)) return nullptr;
        return PyFloat_FromDouble(std::atan(static_cast<double>(v)));
    }

    PyObject* py_atan2(PyObject*, PyObject* args)
    {
        float y, x;
        if (!PyArg_ParseTuple(args, "ff", &y, &x)) return nullptr;
        return PyFloat_FromDouble(std::atan2(static_cast<double>(y), static_cast<double>(x)));
    }

    PyObject* py_sqrt(PyObject*, PyObject* args)
    {
        float v;
        if (!PyArg_ParseTuple(args, "f", &v)) return nullptr;
        return PyFloat_FromDouble(std::sqrt(static_cast<double>(v)));
    }

    PyObject* py_abs(PyObject*, PyObject* args)
    {
        float v;
        if (!PyArg_ParseTuple(args, "f", &v)) return nullptr;
        return PyFloat_FromDouble(std::fabs(static_cast<double>(v)));
    }

    PyObject* py_pow(PyObject*, PyObject* args)
    {
        float base, exp;
        if (!PyArg_ParseTuple(args, "ff", &base, &exp)) return nullptr;
        return PyFloat_FromDouble(std::pow(static_cast<double>(base), static_cast<double>(exp)));
    }

    PyObject* py_floor(PyObject*, PyObject* args)
    {
        float v;
        if (!PyArg_ParseTuple(args, "f", &v)) return nullptr;
        return PyFloat_FromDouble(std::floor(static_cast<double>(v)));
    }

    PyObject* py_ceil(PyObject*, PyObject* args)
    {
        float v;
        if (!PyArg_ParseTuple(args, "f", &v)) return nullptr;
        return PyFloat_FromDouble(std::ceil(static_cast<double>(v)));
    }

    PyObject* py_round(PyObject*, PyObject* args)
    {
        float v;
        if (!PyArg_ParseTuple(args, "f", &v)) return nullptr;
        return PyFloat_FromDouble(std::round(static_cast<double>(v)));
    }

    PyObject* py_sign(PyObject*, PyObject* args)
    {
        float v;
        if (!PyArg_ParseTuple(args, "f", &v)) return nullptr;
        float s = (v > 0.0f) ? 1.0f : ((v < 0.0f) ? -1.0f : 0.0f);
        return PyFloat_FromDouble(static_cast<double>(s));
    }

    PyObject* py_min(PyObject*, PyObject* args)
    {
        float a, b;
        if (!PyArg_ParseTuple(args, "ff", &a, &b)) return nullptr;
        return PyFloat_FromDouble(static_cast<double>(std::fmin(a, b)));
    }

    PyObject* py_max(PyObject*, PyObject* args)
    {
        float a, b;
        if (!PyArg_ParseTuple(args, "ff", &a, &b)) return nullptr;
        return PyFloat_FromDouble(static_cast<double>(std::fmax(a, b)));
    }

    PyObject* py_pi(PyObject*, PyObject*)
    {
        return PyFloat_FromDouble(3.14159265358979323846);
    }

    PyMethodDef MathMethods[] = {
        // Vec3
        { "vec3",           py_vec3,           METH_VARARGS, "Create (x,y,z) tuple. Defaults to (0,0,0)." },
        { "vec3_add",       py_vec3_add,       METH_VARARGS, "Component-wise add two vec3." },
        { "vec3_sub",       py_vec3_sub,       METH_VARARGS, "Component-wise subtract two vec3." },
        { "vec3_mul",       py_vec3_mul,       METH_VARARGS, "Component-wise multiply two vec3." },
        { "vec3_div",       py_vec3_div,       METH_VARARGS, "Component-wise divide two vec3." },
        { "vec3_scale",     py_vec3_scale,     METH_VARARGS, "Scale vec3 by scalar." },
        { "vec3_dot",       py_vec3_dot,       METH_VARARGS, "Dot product of two vec3." },
        { "vec3_cross",     py_vec3_cross,     METH_VARARGS, "Cross product of two vec3." },
        { "vec3_length",    py_vec3_length,    METH_VARARGS, "Length of a vec3." },
        { "vec3_length_sq", py_vec3_length_sq, METH_VARARGS, "Squared length of a vec3." },
        { "vec3_normalize", py_vec3_normalize, METH_VARARGS, "Normalize a vec3." },
        { "vec3_negate",    py_vec3_negate,    METH_VARARGS, "Negate a vec3." },
        { "vec3_lerp",      py_vec3_lerp,      METH_VARARGS, "Linearly interpolate between two vec3." },
        { "vec3_distance",  py_vec3_distance,  METH_VARARGS, "Distance between two vec3." },
        { "vec3_reflect",   py_vec3_reflect,   METH_VARARGS, "Reflect vec3 over a normal." },
        { "vec3_min",       py_vec3_min,       METH_VARARGS, "Component-wise min of two vec3." },
        { "vec3_max",       py_vec3_max,       METH_VARARGS, "Component-wise max of two vec3." },
        // Vec2
        { "vec2",           py_vec2,           METH_VARARGS, "Create (x,y) tuple. Defaults to (0,0)." },
        { "vec2_add",       py_vec2_add,       METH_VARARGS, "Component-wise add two vec2." },
        { "vec2_sub",       py_vec2_sub,       METH_VARARGS, "Component-wise subtract two vec2." },
        { "vec2_scale",     py_vec2_scale,     METH_VARARGS, "Scale vec2 by scalar." },
        { "vec2_dot",       py_vec2_dot,       METH_VARARGS, "Dot product of two vec2." },
        { "vec2_length",    py_vec2_length,    METH_VARARGS, "Length of a vec2." },
        { "vec2_normalize", py_vec2_normalize, METH_VARARGS, "Normalize a vec2." },
        { "vec2_lerp",      py_vec2_lerp,      METH_VARARGS, "Linearly interpolate between two vec2." },
        { "vec2_distance",  py_vec2_distance,  METH_VARARGS, "Distance between two vec2." },
        // Quaternion
        { "quat_from_euler",  py_quat_from_euler,  METH_VARARGS, "Euler (pitch,yaw,roll) radians -> quaternion (x,y,z,w)." },
        { "quat_to_euler",    py_quat_to_euler,    METH_VARARGS, "Quaternion -> Euler (pitch,yaw,roll) radians." },
        { "quat_multiply",    py_quat_multiply,    METH_VARARGS, "Multiply two quaternions." },
        { "quat_normalize",   py_quat_normalize,   METH_VARARGS, "Normalize a quaternion." },
        { "quat_slerp",       py_quat_slerp,       METH_VARARGS, "Slerp between two quaternions." },
        { "quat_inverse",     py_quat_inverse,     METH_VARARGS, "Inverse of a quaternion." },
        { "quat_rotate_vec3", py_quat_rotate_vec3, METH_VARARGS, "Rotate vec3 by quaternion." },
        // Scalar
        { "clamp",       py_clamp,       METH_VARARGS, "Clamp value between min and max." },
        { "lerp",        py_lerp_scalar, METH_VARARGS, "Linearly interpolate between two scalars." },
        { "deg_to_rad",  py_deg_to_rad,  METH_VARARGS, "Convert degrees to radians." },
        { "rad_to_deg",  py_rad_to_deg,  METH_VARARGS, "Convert radians to degrees." },
        // Trigonometric
        { "sin",         py_sin,         METH_VARARGS, "Sine of angle in radians." },
        { "cos",         py_cos,         METH_VARARGS, "Cosine of angle in radians." },
        { "tan",         py_tan,         METH_VARARGS, "Tangent of angle in radians." },
        { "asin",        py_asin,        METH_VARARGS, "Arc sine, returns radians." },
        { "acos",        py_acos,        METH_VARARGS, "Arc cosine, returns radians." },
        { "atan",        py_atan,        METH_VARARGS, "Arc tangent, returns radians." },
        { "atan2",       py_atan2,       METH_VARARGS, "Two-argument arc tangent (y, x), returns radians." },
        // Common math
        { "sqrt",        py_sqrt,        METH_VARARGS, "Square root." },
        { "abs",         py_abs,         METH_VARARGS, "Absolute value." },
        { "pow",         py_pow,         METH_VARARGS, "Raise base to exponent." },
        { "floor",       py_floor,       METH_VARARGS, "Round down to nearest integer." },
        { "ceil",        py_ceil,        METH_VARARGS, "Round up to nearest integer." },
        { "round",       py_round,       METH_VARARGS, "Round to nearest integer." },
        { "sign",        py_sign,        METH_VARARGS, "Sign of value (-1, 0, or 1)." },
        { "min",         py_min,         METH_VARARGS, "Minimum of two values." },
        { "max",         py_max,         METH_VARARGS, "Maximum of two values." },
        { "pi",          py_pi,          METH_NOARGS,  "Return the constant pi." },
        { nullptr, nullptr, 0, nullptr }
    };

    PyModuleDef MathModuleDef = {
        PyModuleDef_HEAD_INIT,
        "engine.math",
        "Math scripting API (Vec2, Vec3, Quat, scalar ops in C++)",
        -1,
        MathMethods
    };
}

PyObject* CreateMathModule()
{
    return PyModule_Create(&MathModuleDef);
}
