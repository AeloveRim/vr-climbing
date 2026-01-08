#pragma once

#include "RE/Skyrim.h"
#include <REL/Relocation.h>
#include <functional>
#include <deque>

// Hand collision system for climbing
// Creates DYNAMIC rigid body colliders on hands when climbing
// Hands try to follow controller but stop at walls (physics-based restraint)
// Based on HIGGS's hand collision implementation

namespace HandCollision {

    //==========================================================================
    // Havok type forward declarations and minimal definitions
    // These match the internal Havok/Bethesda structures that aren't fully
    // exposed in CommonLibSSE-NG
    //==========================================================================

    // Simple aligned vector for Havok (16-byte aligned)
    struct alignas(16) HkVector4 {
        float x, y, z, w;
    };

    // Simple aligned quaternion for Havok
    struct alignas(16) HkQuaternion {
        float x, y, z, w;
    };

    // Havok motion types (from hkpMotion::MotionType)
    enum class HkpMotionType : uint8_t {
        Invalid = 0,
        Dynamic = 1,
        SphereInertia = 2,
        BoxInertia = 3,
        Keyframed = 4,
        Fixed = 5,
        ThinBoxInertia = 6,
        Character = 7
    };

    // Havok collidable quality types
    enum class HkpCollidableQualityType : int8_t {
        Invalid = -1,
        Fixed = 0,
        Keyframed = 1,
        Debris = 2,
        DebrisSimpleToi = 3,
        Moving = 4,
        Critical = 5,
        Bullet = 6,
        User = 7,
        Character = 8,
        KeyframedReporting = 9
    };

    // Havok solver deactivation
    enum class HkpSolverDeactivation : uint8_t {
        Invalid = 0,
        Off = 1,
        Low = 2,
        Medium = 3,
        High = 4,
        Max = 5
    };

    // Havok entity activation
    enum class HkpEntityActivation : uint32_t {
        DoNotActivate = 0,
        DoActivate = 1
    };

    // Internal hkpRigidBodyCinfo - Havok's construction info for rigid bodies
    // This is the underlying Havok structure (size 0xE0)
    struct hkpRigidBodyCinfo {
        uint8_t  pad00[0x10];           // 00
        HkVector4 position;              // 10
        HkQuaternion rotation;           // 20
        HkVector4 linearVelocity;        // 30
        HkVector4 angularVelocity;       // 40
        uint8_t  pad50[0x10];           // 50
        void*    shape;                  // 60 - hkpShape*
        uint32_t collisionFilterInfo;    // 68
        uint8_t  pad6C[0x04];           // 6C
        uint8_t  pad70[0x28];           // 70
        float    mass;                   // 98
        float    friction;               // 9C
        float    restitution;            // A0
        uint8_t  padA4[0x04];           // A4
        float    linearDamping;          // A8
        float    angularDamping;         // AC
        uint8_t  padB0[0x08];           // B0
        float    maxLinearVelocity;      // B8
        float    maxAngularVelocity;     // BC
        uint8_t  padC0[0x04];           // C0
        int8_t   motionType;             // C4 - HkpMotionType
        bool     enableDeactivation;     // C5
        uint8_t  padC6[0x02];           // C6
        int8_t   solverDeactivation;     // C8 - HkpSolverDeactivation
        uint8_t  padC9[0x07];           // C9
        int8_t   qualityType;            // D0 - HkpCollidableQualityType
        uint8_t  padD1[0x0F];           // D1
    };
    static_assert(sizeof(hkpRigidBodyCinfo) == 0xE0);

    // Skyrim's bhkRigidBodyCinfo - wraps hkpRigidBodyCinfo with additional fields
    struct bhkRigidBodyCinfo {
        uint32_t collisionFilterInfo;    // 00 - initd to 0
        void*    shape;                  // 08 - hkpShape*, initd to 0
        uint8_t  unk10;                  // 10 - initd to 1
        uint8_t  pad11[0x07];            // 11
        uint64_t unk18;                  // 18 - initd to 0
        uint32_t unk20;                  // 20 - initd to 0
        float    unk24;                  // 24 - initd to -0
        uint8_t  unk28;                  // 28 - initd to 1
        uint8_t  pad29;                  // 29
        uint16_t unk2A;                  // 2A - initd to -1 (quality type?)
        uint8_t  pad2C[0x04];            // 2C
        hkpRigidBodyCinfo hkCinfo;       // 30 - size == 0xE0
    };
    static_assert(sizeof(bhkRigidBodyCinfo) == 0x110);

    //==========================================================================
    // Function pointer types for Havok/Bethesda functions
    // Using void* for opaque Havok types we don't need to access internally
    //==========================================================================

    // bhkBoxShape constructor - creates a box collision shape
    using bhkBoxShape_ctor_t = void(*)(void* _this, HkVector4* halfExtents);

    // bhkRigidBody constructor - creates the Bethesda wrapper for a rigid body
    using bhkRigidBody_ctor_t = void(*)(void* _this, bhkRigidBodyCinfo* cInfo);

    // bhkRigidBodyCinfo constructor - initializes construction info with defaults
    using bhkRigidBodyCinfo_ctor_t = void(*)(bhkRigidBodyCinfo* _this);

    // Add/remove entity from havok world
    using hkpWorld_AddEntity_t = void* (*)(void* world, void* entity, HkpEntityActivation activation);
    using hkpWorld_RemoveEntity_t = void* (*)(void* world, bool* ret, void* entity);

    // Update collision filter after changing filter info
    using hkpWorld_UpdateCollisionFilterOnEntity_t = void(*)(void* world, void* entity, int updateMode, int updateShapeCollectionFilter);

    // Activate/deactivate rigid body
    using bhkRigidBody_setActivated_t = void(*)(RE::bhkRigidBody* rigidBody, bool activate);

    // Apply hard keyframe - forces body to position in one step (calculates required velocity)
    using ApplyHardKeyFrame_t = void(*)(const HkVector4& nextPosition, const HkQuaternion& nextOrientation, float invDeltaTime, void* body);

    // Set motion type on rigid body
    using bhkRigidBody_setMotionType_t = void(*)(RE::bhkRigidBody* body, int32_t motionType, int32_t activationType, int32_t collisionFilterUpdateMode);

    // Create hkpRigidBody directly (returns hkpRigidBody*)
    using hkpRigidBody_ctor_t = void*(*)(hkpRigidBodyCinfo* cInfo);

    // Get/set position directly on hkpRigidBody
    using hkpRigidBody_getPosition_t = void(*)(void* body, HkVector4* outPosition);
    using hkpRigidBody_setLinearVelocity_t = void(*)(void* body, const HkVector4* velocity);

    //==========================================================================
    // Function pointers (addresses for SkyrimVR 1.4.15)
    // These are from HIGGS's offsets.cpp
    //==========================================================================

    inline REL::Relocation<bhkBoxShape_ctor_t> bhkBoxShape_ctor{ REL::Offset(0x2AEB70) };
    inline REL::Relocation<bhkRigidBody_ctor_t> bhkRigidBody_ctor{ REL::Offset(0x2AEC80) };
    inline REL::Relocation<bhkRigidBodyCinfo_ctor_t> bhkRigidBodyCinfo_ctor{ REL::Offset(0xE06110) };
    inline REL::Relocation<hkpWorld_AddEntity_t> hkpWorld_AddEntity{ REL::Offset(0xAB0CB0) };
    inline REL::Relocation<hkpWorld_RemoveEntity_t> hkpWorld_RemoveEntity{ REL::Offset(0xAB0E50) };
    inline REL::Relocation<hkpWorld_UpdateCollisionFilterOnEntity_t> hkpWorld_UpdateCollisionFilterOnEntity{ REL::Offset(0xAB3110) };
    inline REL::Relocation<bhkRigidBody_setActivated_t> bhkRigidBody_setActivated{ REL::Offset(0xE085D0) };
    inline REL::Relocation<ApplyHardKeyFrame_t> applyHardKeyFrame{ REL::Offset(0xAF6DD0) };
    inline REL::Relocation<bhkRigidBody_setMotionType_t> bhkRigidBody_setMotionType{ REL::Offset(0xE08040) };
    inline REL::Relocation<hkpRigidBody_ctor_t> hkpRigidBody_ctor{ REL::Offset(0xAA89C0) };

    // Global pointers
    inline REL::Relocation<float*> g_havokWorldScale{ REL::Offset(0x15B78F4) };
    inline REL::Relocation<float*> g_inverseHavokWorldScale{ REL::Offset(0x15ADFE8) };
    inline REL::Relocation<float*> g_deltaTime{ REL::Offset(0x1EC8278) };

    // Constants for motion type changes
    constexpr int32_t HK_ENTITY_ACTIVATION_DO_ACTIVATE = 1;
    constexpr int32_t HK_UPDATE_FILTER_ON_ENTITY_FULL_CHECK = 0;

    //==========================================================================
    // hkpRigidBody accessor - get underlying Havok body from bhkRigidBody
    // The hkpRigidBody pointer is at offset 0x10 in bhkRigidBody
    //==========================================================================

    inline void* GetHkpRigidBody(RE::bhkRigidBody* bhkBody) {
        if (!bhkBody) return nullptr;
        return bhkBody->GetRigidBody();
    }

    // Get position from hkpRigidBody (position is at offset 0x200 in hkpRigidBody)
    inline HkVector4 GetHkpRigidBodyPosition(void* hkBody) {
        HkVector4 pos = { 0.0f, 0.0f, 0.0f, 0.0f };
        if (!hkBody) return pos;
        // hkpRigidBody::m_motion is at offset 0x20
        // hkpMotion::m_motionState is at offset 0x40
        // hkMotionState::m_transform is at offset 0x00
        // hkTransform::m_translation is at offset 0x30
        // Total: 0x20 + 0x40 + 0x00 + 0x30 = 0x90
        // Actually from RE: getPosition is virtual, let's use the transform directly
        // hkpRigidBody stores transform at: m_motion (0x20) -> m_motionState (offset in motion) -> transform
        // Simpler: use the collidable's transform: offset 0x40 for m_collidable, then 0x30 for transform translation
        // Let's just read from a known working offset based on HIGGS
        void* transform = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(hkBody) + 0x40 + 0x30);
        pos = *reinterpret_cast<HkVector4*>(transform);
        return pos;
    }

    // Set linear velocity on hkpRigidBody
    // hkpRigidBody::setLinearVelocity modifies m_motion.m_linearVelocity
    inline void SetHkpRigidBodyLinearVelocity(void* hkBody, const HkVector4& velocity) {
        if (!hkBody) return;
        // m_motion is at offset 0x20, m_linearVelocity is at offset 0x60 within motion
        // Total: 0x20 + 0x60 = 0x80
        HkVector4* velPtr = reinterpret_cast<HkVector4*>(reinterpret_cast<uintptr_t>(hkBody) + 0x80);
        *velPtr = velocity;
    }

    //==========================================================================
    // Hand Collision Configuration
    //==========================================================================

    struct HandCollisionConfig {
        // Hand collision box dimensions (in Skyrim units, will be scaled to Havok)
        RE::NiPoint3 boxHalfExtents = { 5.0f, 1.5f, 9.0f };  // Roughly palm-sized
        RE::NiPoint3 boxOffset = { 0.0f, -0.5f, 8.6f };      // Offset from hand node

        // Collision layer - use layer 5 (Weapon) which already collides with static world
        uint32_t collisionLayer = 5;

        // Delay before enabling collision after creation (prevents initial penetration issues)
        float enableDelay = 0.1f;

        // Dynamic body settings
        float mass = 1.0f;                    // Mass of hand collider
        float linearDamping = 0.5f;           // Velocity damping
        float angularDamping = 0.5f;          // Angular damping
        float maxLinearVelocity = 50.0f;      // Max velocity in m/s (Havok units)
        float friction = 0.5f;                // Friction coefficient
        float restitution = 0.0f;             // Bounciness (0 = no bounce)

        // Hand tracking settings
        float velocityGain = 30.0f;           // How aggressively hand tries to reach controller (velocity multiplier)
        float maxHandDistance = 30.0f;        // Max distance (Skyrim units) before losing grip
        int deviationFrameCount = 5;          // Number of frames to average deviation over
    };

    //==========================================================================
    // Lose Grip Callback
    //==========================================================================

    using LoseGripCallback = std::function<void(bool isLeft)>;

    //==========================================================================
    // Hand Collision Manager
    //==========================================================================

    class HandCollisionManager {
    public:
        static HandCollisionManager* GetSingleton();

        // Initialize/shutdown
        void Initialize();
        void Shutdown();

        // Enable/disable hand colliders (call when climbing starts/stops)
        void EnableHandColliders(RE::bhkWorld* world);
        void DisableHandColliders();

        // Update hand collider positions (call every physics frame while climbing)
        void UpdateHandColliders(RE::bhkWorld* world);

        // Check if colliders are active
        bool AreCollidersActive() const { return m_leftHkBody != nullptr || m_rightHkBody != nullptr; }

        // Configuration
        HandCollisionConfig& GetConfig() { return m_config; }

        // Get the adjusted (physics-constrained) hand positions
        // Returns position in Skyrim world units
        RE::NiPoint3 GetAdjustedLeftHandPosition() const { return m_adjustedLeftHandPos; }
        RE::NiPoint3 GetAdjustedRightHandPosition() const { return m_adjustedRightHandPos; }

        // Check if hand has deviated too far from controller (should lose grip)
        bool HasLeftHandLostGrip() const { return m_leftHandLostGrip; }
        bool HasRightHandLostGrip() const { return m_rightHandLostGrip; }

        // Reset lost grip flags (call after handling the lose grip event)
        void ResetLostGripFlags() { m_leftHandLostGrip = false; m_rightHandLostGrip = false; }

        // Set callback for when a hand loses grip
        void SetLoseGripCallback(LoseGripCallback callback) { m_loseGripCallback = callback; }

    private:
        HandCollisionManager() = default;
        ~HandCollisionManager() = default;
        HandCollisionManager(const HandCollisionManager&) = delete;
        HandCollisionManager& operator=(const HandCollisionManager&) = delete;

        // Create/remove individual hand collider (returns raw hkpRigidBody*)
        void* CreateHandCollider(RE::bhkWorld* world, bool isLeft);
        void RemoveHandCollider(RE::bhkWorld* world, void*& hkBody);

        // Compute target collision transform for a hand (where controller wants to be)
        void ComputeHandCollisionTransform(bool isLeft, HkVector4& outPosition, HkQuaternion& outRotation);

        // Get player collision group from character controller
        uint16_t GetPlayerCollisionGroup();

        // Check deviation and update lost grip state
        void UpdateDeviationTracking(bool isLeft, const RE::NiPoint3& controllerPos, const RE::NiPoint3& actualPos);

        // Configuration
        HandCollisionConfig m_config;

        // Hand rigid bodies - raw hkpRigidBody pointers (nullptr when not active)
        void* m_leftHkBody = nullptr;
        void* m_rightHkBody = nullptr;

        // Adjusted hand positions (where hands actually are after physics)
        RE::NiPoint3 m_adjustedLeftHandPos;
        RE::NiPoint3 m_adjustedRightHandPos;

        // Controller target positions (where controllers want hands to be)
        RE::NiPoint3 m_targetLeftHandPos;
        RE::NiPoint3 m_targetRightHandPos;

        // Deviation tracking for lose grip detection
        std::deque<float> m_leftHandDeviations;
        std::deque<float> m_rightHandDeviations;
        bool m_leftHandLostGrip = false;
        bool m_rightHandLostGrip = false;

        // Lose grip callback
        LoseGripCallback m_loseGripCallback;

        // Track frames since creation for enable delay
        int m_framesSinceCreation = 0;
        bool m_collisionEnabled = false;

        // World the colliders are in (for cleanup)
        RE::bhkWorld* m_currentWorld = nullptr;

        bool m_initialized = false;
    };

} // namespace HandCollision
