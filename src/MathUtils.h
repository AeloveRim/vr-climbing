#pragma once

#include <RE/Skyrim.h>
#include <cmath>

namespace MathUtils
{
    // Convert a 3x3 rotation matrix (hkRotation) to a quaternion (hkQuaternion)
    inline void RotationMatrixToQuaternion(const RE::hkRotation& rot, RE::hkQuaternion& outQuat)
    {
        float trace = rot.col0.quad.m128_f32[0] + rot.col1.quad.m128_f32[1] + rot.col2.quad.m128_f32[2];
        float x, y, z, w;

        if (trace > 0) {
            float s = 0.5f / sqrtf(trace + 1.0f);
            w = 0.25f / s;
            x = (rot.col1.quad.m128_f32[2] - rot.col2.quad.m128_f32[1]) * s;
            y = (rot.col2.quad.m128_f32[0] - rot.col0.quad.m128_f32[2]) * s;
            z = (rot.col0.quad.m128_f32[1] - rot.col1.quad.m128_f32[0]) * s;
        } else if (rot.col0.quad.m128_f32[0] > rot.col1.quad.m128_f32[1] && rot.col0.quad.m128_f32[0] > rot.col2.quad.m128_f32[2]) {
            float s = 2.0f * sqrtf(1.0f + rot.col0.quad.m128_f32[0] - rot.col1.quad.m128_f32[1] - rot.col2.quad.m128_f32[2]);
            w = (rot.col1.quad.m128_f32[2] - rot.col2.quad.m128_f32[1]) / s;
            x = 0.25f * s;
            y = (rot.col1.quad.m128_f32[0] + rot.col0.quad.m128_f32[1]) / s;
            z = (rot.col2.quad.m128_f32[0] + rot.col0.quad.m128_f32[2]) / s;
        } else if (rot.col1.quad.m128_f32[1] > rot.col2.quad.m128_f32[2]) {
            float s = 2.0f * sqrtf(1.0f + rot.col1.quad.m128_f32[1] - rot.col0.quad.m128_f32[0] - rot.col2.quad.m128_f32[2]);
            w = (rot.col2.quad.m128_f32[0] - rot.col0.quad.m128_f32[2]) / s;
            x = (rot.col1.quad.m128_f32[0] + rot.col0.quad.m128_f32[1]) / s;
            y = 0.25f * s;
            z = (rot.col2.quad.m128_f32[1] + rot.col1.quad.m128_f32[2]) / s;
        } else {
            float s = 2.0f * sqrtf(1.0f + rot.col2.quad.m128_f32[2] - rot.col0.quad.m128_f32[0] - rot.col1.quad.m128_f32[1]);
            w = (rot.col0.quad.m128_f32[1] - rot.col1.quad.m128_f32[0]) / s;
            x = (rot.col2.quad.m128_f32[0] + rot.col0.quad.m128_f32[2]) / s;
            y = (rot.col2.quad.m128_f32[1] + rot.col1.quad.m128_f32[2]) / s;
            z = 0.25f * s;
        }

        outQuat.vec.quad.m128_f32[0] = x;
        outQuat.vec.quad.m128_f32[1] = y;
        outQuat.vec.quad.m128_f32[2] = z;
        outQuat.vec.quad.m128_f32[3] = w;
    }
}
