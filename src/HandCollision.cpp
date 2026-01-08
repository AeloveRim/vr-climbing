#include "HandCollision.h"
#include "util/VRNodes.h"
#include "HavokUtils.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <numeric>

namespace HandCollision {

    // Size of bhkBoxShape structure (from HIGGS)
    constexpr size_t SIZEOF_BHK_BOX_SHAPE = 0x28;

    // Size of bhkRigidBody structure
    constexpr size_t SIZEOF_BHK_RIGID_BODY = 0x40;

    // Offset to shape pointer in bhkShape (hkRefPtr<hkpShape>)
    constexpr size_t OFFSET_BHK_SHAPE_HKSHAPE = 0x10;

    // Collision filter update mode (local constant to avoid conflict with header)
    constexpr int LOCAL_HK_UPDATE_COLLECTION_FILTER_PROCESS_SHAPE_COLLECTIONS = 0;

    HandCollisionManager* HandCollisionManager::GetSingleton() {
        static HandCollisionManager instance;
        return &instance;
    }

    void HandCollisionManager::Initialize() {
        if (m_initialized) {
            spdlog::warn("HandCollisionManager already initialized");
            return;
        }
        m_initialized = true;
        spdlog::info("HandCollisionManager initialized");
    }

    void HandCollisionManager::Shutdown() {
        if (!m_initialized) {
            return;
        }

        // Clean up any active colliders
        DisableHandColliders();

        m_initialized = false;
        spdlog::info("HandCollisionManager shut down");
    }

    uint16_t HandCollisionManager::GetPlayerCollisionGroup() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return 0;
        }

        auto* controller = player->GetCharController();
        if (!controller) {
            return 0;
        }

        // Get collision filter info and extract group (upper 16 bits)
        uint32_t filterInfo = 0;
        controller->GetCollisionFilterInfo(filterInfo);
        return static_cast<uint16_t>(filterInfo >> 16);
    }

    // Helper to convert NiMatrix3 to our HkQuaternion
    static void MatrixToHkQuaternion(const RE::NiMatrix3& mat, HkQuaternion& quat) {
        // Use the existing HavokUtils function to convert to RE::hkQuaternion
        RE::hkQuaternion reQuat;
        HavokUtils::MatrixToQuaternion(mat, reQuat);

        // Copy to our HkQuaternion
        quat.x = reQuat.vec.quad.m128_f32[0];
        quat.y = reQuat.vec.quad.m128_f32[1];
        quat.z = reQuat.vec.quad.m128_f32[2];
        quat.w = reQuat.vec.quad.m128_f32[3];
    }

    void HandCollisionManager::ComputeHandCollisionTransform(bool isLeft, HkVector4& outPosition, HkQuaternion& outRotation) {
        RE::NiAVObject* handNode = isLeft ? VRNodes::GetLeftHand() : VRNodes::GetRightHand();

        if (!handNode) {
            // Fallback to identity
            outPosition = { 0.0f, 0.0f, 0.0f, 0.0f };
            outRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
            spdlog::warn("ComputeHandCollisionTransform: {} hand node is NULL!", isLeft ? "Left" : "Right");
            return;
        }

        const RE::NiTransform& handTransform = handNode->world;

        // Apply offset (mirror X for left hand)
        RE::NiPoint3 offset = m_config.boxOffset;
        if (isLeft) {
            offset.x *= -1.0f;
        }

        // Get havok world scale
        float havokScale = *g_havokWorldScale;

        // Transform offset by hand rotation (offset is in local hand space)
        RE::NiPoint3 worldOffset;
        worldOffset.x = handTransform.rotate.entry[0][0] * offset.x +
                        handTransform.rotate.entry[0][1] * offset.y +
                        handTransform.rotate.entry[0][2] * offset.z;
        worldOffset.y = handTransform.rotate.entry[1][0] * offset.x +
                        handTransform.rotate.entry[1][1] * offset.y +
                        handTransform.rotate.entry[1][2] * offset.z;
        worldOffset.z = handTransform.rotate.entry[2][0] * offset.x +
                        handTransform.rotate.entry[2][1] * offset.y +
                        handTransform.rotate.entry[2][2] * offset.z;

        // Final position in Skyrim units (store for tracking)
        RE::NiPoint3 finalPosSkyrim = handTransform.translate + worldOffset;
        if (isLeft) {
            m_targetLeftHandPos = finalPosSkyrim;
        } else {
            m_targetRightHandPos = finalPosSkyrim;
        }

        // Convert to Havok units
        outPosition.x = finalPosSkyrim.x * havokScale;
        outPosition.y = finalPosSkyrim.y * havokScale;
        outPosition.z = finalPosSkyrim.z * havokScale;
        outPosition.w = 0.0f;

        // Convert rotation matrix to quaternion
        MatrixToHkQuaternion(handTransform.rotate, outRotation);
    }

    void* HandCollisionManager::CreateHandCollider(RE::bhkWorld* world, bool isLeft) {
        if (!world) {
            spdlog::error("CreateHandCollider: world is null");
            return nullptr;
        }

        // Get the underlying hkpWorld from bhkWorld
        void* hkWorld = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(world) + 0x10);
        if (!hkWorld) {
            spdlog::error("CreateHandCollider: hkpWorld is null");
            return nullptr;
        }

        float havokScale = *g_havokWorldScale;

        // 1. Allocate box shape
        void* handShape = RE::malloc(SIZEOF_BHK_BOX_SHAPE);
        if (!handShape) {
            spdlog::error("CreateHandCollider: Failed to allocate bhkBoxShape");
            return nullptr;
        }
        memset(handShape, 0, SIZEOF_BHK_BOX_SHAPE);

        // Convert half extents to Havok scale
        HkVector4 halfExtents;
        halfExtents.x = m_config.boxHalfExtents.x * havokScale;
        halfExtents.y = m_config.boxHalfExtents.y * havokScale;
        halfExtents.z = m_config.boxHalfExtents.z * havokScale;
        halfExtents.w = 0.0f;

        // Log before constructor
        spdlog::info("CreateHandCollider: About to call bhkBoxShape_ctor at {:p}", reinterpret_cast<void*>(bhkBoxShape_ctor.address()));
        spdlog::info("CreateHandCollider: handShape allocated at {:p}, size=0x{:X}", handShape, static_cast<unsigned int>(SIZEOF_BHK_BOX_SHAPE));
        spdlog::info("CreateHandCollider: halfExtents = ({:.4f}, {:.4f}, {:.4f}, {:.4f})",
            halfExtents.x, halfExtents.y, halfExtents.z, halfExtents.w);

        // Call bhkBoxShape constructor
        bhkBoxShape_ctor(handShape, &halfExtents);

        // Debug: dump first 0x28 bytes of handShape after ctor
        spdlog::info("CreateHandCollider: After bhkBoxShape_ctor:");
        uint64_t* ptr64 = reinterpret_cast<uint64_t*>(handShape);
        for (int i = 0; i < 5; i++) {
            spdlog::info("  +0x{:02X}: 0x{:016X}", i * 8, ptr64[i]);
        }

        // Get the underlying hkpShape from bhkShape (at offset 0x10)
        void* hkShape = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(handShape) + OFFSET_BHK_SHAPE_HKSHAPE);
        spdlog::info("CreateHandCollider: hkShape (at offset 0x10) = {:p}", hkShape);

        if (!hkShape) {
            spdlog::error("CreateHandCollider: bhkBoxShape constructor didn't create hkpShape!");
            RE::free(handShape);
            return nullptr;
        }

        // Check if hkShape looks valid (should be heap memory, not code)
        uintptr_t hkShapeAddr = reinterpret_cast<uintptr_t>(hkShape);
        uintptr_t exeBase = REL::Module::get().base();
        if (hkShapeAddr >= exeBase && hkShapeAddr < exeBase + 0x2000000) {
            spdlog::error("CreateHandCollider: hkShape {:p} appears to be in EXE memory (base {:p})! Likely garbage.",
                hkShape, reinterpret_cast<void*>(exeBase));
            RE::free(handShape);
            return nullptr;
        }

        // 2. Create rigid body construction info
        bhkRigidBodyCinfo cInfo;
        bhkRigidBodyCinfo_ctor(&cInfo);

        // *** KEY CHANGE: Configure as DYNAMIC (physics-driven, but we set velocity) ***
        cInfo.hkCinfo.motionType = static_cast<int8_t>(HkpMotionType::Dynamic);
        cInfo.hkCinfo.enableDeactivation = false;
        cInfo.hkCinfo.solverDeactivation = static_cast<int8_t>(HkpSolverDeactivation::Off);
        cInfo.hkCinfo.qualityType = static_cast<int8_t>(HkpCollidableQualityType::Moving);

        // Set physics properties for dynamic body
        cInfo.hkCinfo.mass = m_config.mass;
        cInfo.hkCinfo.friction = m_config.friction;
        cInfo.hkCinfo.restitution = m_config.restitution;
        cInfo.hkCinfo.linearDamping = m_config.linearDamping;
        cInfo.hkCinfo.angularDamping = m_config.angularDamping;
        cInfo.hkCinfo.maxLinearVelocity = m_config.maxLinearVelocity;
        cInfo.hkCinfo.maxAngularVelocity = 10.0f;

        // Set shape
        cInfo.shape = hkShape;
        cInfo.hkCinfo.shape = hkShape;

        // Set initial position and rotation
        HkVector4 initialPos;
        HkQuaternion initialRot;
        ComputeHandCollisionTransform(isLeft, initialPos, initialRot);
        cInfo.hkCinfo.position = initialPos;
        cInfo.hkCinfo.rotation = initialRot;
        cInfo.hkCinfo.linearVelocity = { 0.0f, 0.0f, 0.0f, 0.0f };
        cInfo.hkCinfo.angularVelocity = { 0.0f, 0.0f, 0.0f, 0.0f };

        // Build collision filter info:
        // - Layer in lower 5 bits
        // - Collision group in upper 16 bits
        // - Bit 14 = disabled flag (we'll enable after delay)
        // - Bit 15 = collide with same group that has bit 15 set
        uint16_t playerGroup = GetPlayerCollisionGroup();
        uint32_t filterInfo = (static_cast<uint32_t>(playerGroup) << 16) | m_config.collisionLayer;
        filterInfo |= (1 << 15);  // Collide with same group that has bit 15 set
        filterInfo |= (1 << 14);  // Initially DISABLED
        cInfo.collisionFilterInfo = filterInfo;
        cInfo.hkCinfo.collisionFilterInfo = filterInfo;

        // 3. Dump hkCinfo before calling constructor
        spdlog::info("CreateHandCollider: About to call hkpRigidBody_ctor at {:p}",
            reinterpret_cast<void*>(hkpRigidBody_ctor.address()));
        spdlog::info("CreateHandCollider: hkCinfo at {:p}, size=0x{:X}",
            static_cast<void*>(&cInfo.hkCinfo), static_cast<unsigned int>(sizeof(hkpRigidBodyCinfo)));
        spdlog::info("  position: ({:.4f}, {:.4f}, {:.4f})",
            cInfo.hkCinfo.position.x, cInfo.hkCinfo.position.y, cInfo.hkCinfo.position.z);
        spdlog::info("  rotation: ({:.4f}, {:.4f}, {:.4f}, {:.4f})",
            cInfo.hkCinfo.rotation.x, cInfo.hkCinfo.rotation.y, cInfo.hkCinfo.rotation.z, cInfo.hkCinfo.rotation.w);
        spdlog::info("  shape: {:p}", cInfo.hkCinfo.shape);
        spdlog::info("  mass: {:.2f}, friction: {:.2f}", cInfo.hkCinfo.mass, cInfo.hkCinfo.friction);
        spdlog::info("  motionType: {}, qualityType: {}",
            static_cast<int>(cInfo.hkCinfo.motionType), static_cast<int>(cInfo.hkCinfo.qualityType));
        spdlog::info("  collisionFilterInfo: 0x{:08X}", cInfo.hkCinfo.collisionFilterInfo);

        // Dump raw bytes around shape offset to verify layout
        uint64_t* cinfoPtr = reinterpret_cast<uint64_t*>(&cInfo.hkCinfo);
        spdlog::info("CreateHandCollider: hkCinfo raw dump (first 0x70 bytes):");
        for (int i = 0; i < 14; i++) {
            spdlog::info("  +0x{:02X}: 0x{:016X}", i * 8, cinfoPtr[i]);
        }

        // Create hkpRigidBody directly using Havok functions (skip bhkRigidBody wrapper)
        void* hkBody = hkpRigidBody_ctor(&cInfo.hkCinfo);
        if (!hkBody) {
            spdlog::error("CreateHandCollider: hkpRigidBody_ctor returned null!");
            RE::free(handShape);
            return nullptr;
        }

        spdlog::info("CreateHandCollider: hkWorld={:p}, hkBody={:p}", hkWorld, hkBody);

        // 4. Add to world
        void* addResult = hkpWorld_AddEntity(hkWorld, hkBody, HkpEntityActivation::DoActivate);
        spdlog::info("CreateHandCollider: hkpWorld_AddEntity returned {:p}", addResult);

        // Log detailed creation info for debugging
        spdlog::info("CreateHandCollider: Created {} hand collider (DYNAMIC):", isLeft ? "left" : "right");
        spdlog::info("  - Layer: {}, FilterInfo: 0x{:08X}", m_config.collisionLayer, filterInfo);
        spdlog::info("  - Mass: {:.2f}, Friction: {:.2f}, Damping: {:.2f}",
            m_config.mass, m_config.friction, m_config.linearDamping);
        spdlog::info("  - Havok scale: {:.6f}", havokScale);
        spdlog::info("  - Box half extents (Skyrim): ({:.1f}, {:.1f}, {:.1f})",
            m_config.boxHalfExtents.x, m_config.boxHalfExtents.y, m_config.boxHalfExtents.z);

        return hkBody;  // Return raw hkpRigidBody pointer
    }

    void HandCollisionManager::RemoveHandCollider(RE::bhkWorld* world, void*& hkBody) {
        if (!hkBody) {
            return;
        }

        if (world) {
            // Get hkpWorld from bhkWorld
            void* hkWorld = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(world) + 0x10);
            if (hkWorld) {
                bool ret = false;
                hkpWorld_RemoveEntity(hkWorld, &ret, hkBody);
            }
        }

        hkBody = nullptr;
    }

    void HandCollisionManager::EnableHandColliders(RE::bhkWorld* world) {
        if (!m_initialized) {
            spdlog::warn("EnableHandColliders: Not initialized");
            return;
        }

        if (!world) {
            spdlog::error("EnableHandColliders: world is null");
            return;
        }

        // If already have colliders in a different world, clean them up first
        if (m_currentWorld && m_currentWorld != world) {
            DisableHandColliders();
        }

        // Create colliders if not already active
        if (!m_leftHkBody) {
            m_leftHkBody = CreateHandCollider(world, true);
        }
        if (!m_rightHkBody) {
            m_rightHkBody = CreateHandCollider(world, false);
        }

        m_currentWorld = world;
        m_framesSinceCreation = 0;
        m_collisionEnabled = false;

        // Clear deviation tracking
        m_leftHandDeviations.clear();
        m_rightHandDeviations.clear();
        m_leftHandLostGrip = false;
        m_rightHandLostGrip = false;

        spdlog::info("EnableHandColliders: Hand colliders enabled (DYNAMIC mode)");
    }

    void HandCollisionManager::DisableHandColliders() {
        if (m_leftHkBody || m_rightHkBody) {
            RemoveHandCollider(m_currentWorld, m_leftHkBody);
            RemoveHandCollider(m_currentWorld, m_rightHkBody);
            m_currentWorld = nullptr;
            m_collisionEnabled = false;
            m_framesSinceCreation = 0;
            m_leftHandDeviations.clear();
            m_rightHandDeviations.clear();
            spdlog::info("DisableHandColliders: Hand colliders disabled");
        }
    }

    void HandCollisionManager::UpdateDeviationTracking(bool isLeft, const RE::NiPoint3& controllerPos, const RE::NiPoint3& actualPos) {
        // Calculate distance between controller and actual hand position
        RE::NiPoint3 diff = actualPos - controllerPos;
        float distance = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

        // Get the appropriate deque
        std::deque<float>& deviations = isLeft ? m_leftHandDeviations : m_rightHandDeviations;
        bool& lostGrip = isLeft ? m_leftHandLostGrip : m_rightHandLostGrip;

        // Add new deviation, remove old if over limit
        deviations.push_front(distance);
        while (static_cast<int>(deviations.size()) > m_config.deviationFrameCount) {
            deviations.pop_back();
        }

        // Calculate average deviation
        if (!deviations.empty()) {
            float avgDeviation = std::accumulate(deviations.begin(), deviations.end(), 0.0f) / deviations.size();

            // Check if exceeded max distance
            if (avgDeviation > m_config.maxHandDistance) {
                if (!lostGrip) {
                    lostGrip = true;
                    spdlog::info("UpdateDeviationTracking: {} hand lost grip! Avg deviation: {:.1f} > {:.1f}",
                        isLeft ? "Left" : "Right", avgDeviation, m_config.maxHandDistance);

                    // Fire callback if set
                    if (m_loseGripCallback) {
                        m_loseGripCallback(isLeft);
                    }
                }
            }
        }
    }

    void HandCollisionManager::UpdateHandColliders(RE::bhkWorld* world) {
        if (!m_leftHkBody && !m_rightHkBody) {
            return;  // No active colliders
        }

        if (!world) {
            return;
        }

        float deltaTime = *g_deltaTime;
        if (deltaTime <= 0.0f) {
            deltaTime = 1.0f / 90.0f;  // Assume 90 FPS if no delta available
        }

        float havokScale = *g_havokWorldScale;
        float invHavokScale = 1.0f / havokScale;

        // Enable collision after delay (if not already enabled)
        if (!m_collisionEnabled) {
            m_framesSinceCreation++;
            if (m_framesSinceCreation > 9) {  // ~0.1 seconds at 90 FPS
                m_collisionEnabled = true;

                // Get hkpWorld for filter update
                void* hkWorld = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(world) + 0x10);
                if (hkWorld) {
                    // Remove the disable bit (bit 14) from filter info for both hands
                    if (m_leftHkBody) {
                        uint32_t* filterInfoPtr = reinterpret_cast<uint32_t*>(
                            reinterpret_cast<uintptr_t>(m_leftHkBody) + 0x40);
                        *filterInfoPtr &= ~(1 << 14);  // Clear disable bit

                        hkpWorld_UpdateCollisionFilterOnEntity(hkWorld, m_leftHkBody,
                            HK_UPDATE_FILTER_ON_ENTITY_FULL_CHECK,
                            LOCAL_HK_UPDATE_COLLECTION_FILTER_PROCESS_SHAPE_COLLECTIONS);
                    }
                    if (m_rightHkBody) {
                        uint32_t* filterInfoPtr = reinterpret_cast<uint32_t*>(
                            reinterpret_cast<uintptr_t>(m_rightHkBody) + 0x40);
                        *filterInfoPtr &= ~(1 << 14);  // Clear disable bit

                        hkpWorld_UpdateCollisionFilterOnEntity(hkWorld, m_rightHkBody,
                            HK_UPDATE_FILTER_ON_ENTITY_FULL_CHECK,
                            LOCAL_HK_UPDATE_COLLECTION_FILTER_PROCESS_SHAPE_COLLECTIONS);
                    }
                }
                spdlog::debug("UpdateHandColliders: Collision now enabled");
            }
        }

        // Update left hand
        if (m_leftHkBody) {
            HkVector4 targetPos;
            HkQuaternion targetRot;
            ComputeHandCollisionTransform(true, targetPos, targetRot);

            // Get current position of the body (we have raw hkpRigidBody pointer directly)
            HkVector4 currentPos = GetHkpRigidBodyPosition(m_leftHkBody);

            // Calculate velocity to reach target position
            // velocity = (target - current) * gain
            HkVector4 velocity;
            velocity.x = (targetPos.x - currentPos.x) * m_config.velocityGain;
            velocity.y = (targetPos.y - currentPos.y) * m_config.velocityGain;
            velocity.z = (targetPos.z - currentPos.z) * m_config.velocityGain;
            velocity.w = 0.0f;

            // Clamp velocity magnitude
            float velMag = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);
            if (velMag > m_config.maxLinearVelocity) {
                float scale = m_config.maxLinearVelocity / velMag;
                velocity.x *= scale;
                velocity.y *= scale;
                velocity.z *= scale;
            }

            // Apply velocity to body
            SetHkpRigidBodyLinearVelocity(m_leftHkBody, velocity);

            // Read back the actual position (after physics)
            HkVector4 actualPos = GetHkpRigidBodyPosition(m_leftHkBody);
            m_adjustedLeftHandPos.x = actualPos.x * invHavokScale;
            m_adjustedLeftHandPos.y = actualPos.y * invHavokScale;
            m_adjustedLeftHandPos.z = actualPos.z * invHavokScale;

            // Update deviation tracking
            UpdateDeviationTracking(true, m_targetLeftHandPos, m_adjustedLeftHandPos);

            // Debug logging
            static int leftLogCounter = 0;
            if (++leftLogCounter >= 90) {
                leftLogCounter = 0;
                RE::NiPoint3 diff = m_adjustedLeftHandPos - m_targetLeftHandPos;
                float deviation = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
                spdlog::info("Left hand - Target: ({:.0f}, {:.0f}, {:.0f}), Actual: ({:.0f}, {:.0f}, {:.0f}), Deviation: {:.1f}",
                    m_targetLeftHandPos.x, m_targetLeftHandPos.y, m_targetLeftHandPos.z,
                    m_adjustedLeftHandPos.x, m_adjustedLeftHandPos.y, m_adjustedLeftHandPos.z,
                    deviation);
            }
        }

        // Update right hand
        if (m_rightHkBody) {
            HkVector4 targetPos;
            HkQuaternion targetRot;
            ComputeHandCollisionTransform(false, targetPos, targetRot);

            // Get current position of the body (we have raw hkpRigidBody pointer directly)
            HkVector4 currentPos = GetHkpRigidBodyPosition(m_rightHkBody);

            // Calculate velocity to reach target position
            HkVector4 velocity;
            velocity.x = (targetPos.x - currentPos.x) * m_config.velocityGain;
            velocity.y = (targetPos.y - currentPos.y) * m_config.velocityGain;
            velocity.z = (targetPos.z - currentPos.z) * m_config.velocityGain;
            velocity.w = 0.0f;

            // Clamp velocity magnitude
            float velMag = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);
            if (velMag > m_config.maxLinearVelocity) {
                float scale = m_config.maxLinearVelocity / velMag;
                velocity.x *= scale;
                velocity.y *= scale;
                velocity.z *= scale;
            }

            // Apply velocity to body
            SetHkpRigidBodyLinearVelocity(m_rightHkBody, velocity);

            // Read back the actual position
            HkVector4 actualPos = GetHkpRigidBodyPosition(m_rightHkBody);
            m_adjustedRightHandPos.x = actualPos.x * invHavokScale;
            m_adjustedRightHandPos.y = actualPos.y * invHavokScale;
            m_adjustedRightHandPos.z = actualPos.z * invHavokScale;

            // Update deviation tracking
            UpdateDeviationTracking(false, m_targetRightHandPos, m_adjustedRightHandPos);

            // Debug logging
            static int rightLogCounter = 45;
            if (++rightLogCounter >= 90) {
                rightLogCounter = 0;
                RE::NiPoint3 diff = m_adjustedRightHandPos - m_targetRightHandPos;
                float deviation = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
                spdlog::info("Right hand - Target: ({:.0f}, {:.0f}, {:.0f}), Actual: ({:.0f}, {:.0f}, {:.0f}), Deviation: {:.1f}",
                    m_targetRightHandPos.x, m_targetRightHandPos.y, m_targetRightHandPos.z,
                    m_adjustedRightHandPos.x, m_adjustedRightHandPos.y, m_adjustedRightHandPos.z,
                    deviation);
            }
        }
    }

} // namespace HandCollision
