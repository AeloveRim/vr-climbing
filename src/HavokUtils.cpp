#include "HavokUtils.h"
#include <cmath>

namespace HavokUtils {

    // Convert NiMatrix3 (3x3 rotation matrix) to hkQuaternion
    // Algorithm from: https://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/
    void MatrixToQuaternion(const RE::NiMatrix3& mat, RE::hkQuaternion& quat) {
        // NiMatrix3 is row-major: entry[row][col]
        float m00 = mat.entry[0][0], m01 = mat.entry[0][1], m02 = mat.entry[0][2];
        float m10 = mat.entry[1][0], m11 = mat.entry[1][1], m12 = mat.entry[1][2];
        float m20 = mat.entry[2][0], m21 = mat.entry[2][1], m22 = mat.entry[2][2];

        float trace = m00 + m11 + m22;
        float x, y, z, w;

        if (trace > 0) {
            float s = 0.5f / sqrtf(trace + 1.0f);
            w = 0.25f / s;
            x = (m21 - m12) * s;
            y = (m02 - m20) * s;
            z = (m10 - m01) * s;
        } else if (m00 > m11 && m00 > m22) {
            float s = 2.0f * sqrtf(1.0f + m00 - m11 - m22);
            w = (m21 - m12) / s;
            x = 0.25f * s;
            y = (m01 + m10) / s;
            z = (m02 + m20) / s;
        } else if (m11 > m22) {
            float s = 2.0f * sqrtf(1.0f + m11 - m00 - m22);
            w = (m02 - m20) / s;
            x = (m01 + m10) / s;
            y = 0.25f * s;
            z = (m12 + m21) / s;
        } else {
            float s = 2.0f * sqrtf(1.0f + m22 - m00 - m11);
            w = (m10 - m01) / s;
            x = (m02 + m20) / s;
            y = (m12 + m21) / s;
            z = 0.25f * s;
        }

        // hkQuaternion stores as (x, y, z, w) in the vec member
        quat.vec.quad.m128_f32[0] = x;
        quat.vec.quad.m128_f32[1] = y;
        quat.vec.quad.m128_f32[2] = z;
        quat.vec.quad.m128_f32[3] = w;
    }
}
