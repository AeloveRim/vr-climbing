#pragma once
// Minimal Havok utilities for constraint operations
// Function pointers to Skyrim's havok functions

#include <REL/Relocation.h>

namespace HavokUtils {

    // hkpKeyFrameUtility::applyHardKeyFrame
    // Forces a rigid body to a specific position/rotation in one physics step
    // Signature: void(const hkVector4& nextPosition, const hkQuaternion& nextOrientation, hkReal invDeltaTime, hkpRigidBody* body)
    using ApplyHardKeyFrame_t = void(*)(const RE::hkVector4&, const RE::hkQuaternion&, float, RE::hkpRigidBody*);

    // Function pointer addresses for Skyrim VR 1.4.15
    // These offsets are from HIGGS's offsets.cpp (reference/higgs/src/RE/offsets.cpp)
    inline REL::Relocation<ApplyHardKeyFrame_t> applyHardKeyFrame{ REL::Offset(0xAF6DD0) };
    inline REL::Relocation<ApplyHardKeyFrame_t> applyHardKeyFrameAsync{ REL::Offset(0xAF7100) };

    // Global pointers
    // g_havokWorldScale: multiply Skyrim coords by this to get Havok coords
    inline REL::Relocation<float*> g_havokWorldScale{ REL::Offset(0x15B78F4) };

    // g_deltaTime: time since last frame
    inline REL::Relocation<float*> g_deltaTime{ REL::Offset(0x1EC8278) };

    // Helper to convert Skyrim position to Havok position
    inline void SkyrimToHavok(const RE::NiPoint3& skyrimPos, RE::hkVector4& havokPos) {
        float scale = *g_havokWorldScale;
        // hkVector4 is 16-byte aligned: (x, y, z, w) as quads
        havokPos.quad.m128_f32[0] = skyrimPos.x * scale;
        havokPos.quad.m128_f32[1] = skyrimPos.y * scale;
        havokPos.quad.m128_f32[2] = skyrimPos.z * scale;
        havokPos.quad.m128_f32[3] = 0.0f;
    }

    // Helper to convert NiMatrix3 rotation to hkQuaternion
    void MatrixToQuaternion(const RE::NiMatrix3& mat, RE::hkQuaternion& quat);
}
