// Not actually used in the mod. Was playing around with physics constraints

#include "ShackleModeManager.h"
#include "HavokUtils.h"
#include "MathUtils.h"
#include "NpcUtils.h"
#include "log.h"

ShackleModeManager* ShackleModeManager::GetSingleton()
{
    static ShackleModeManager instance;
    return &instance;
}

void ShackleModeManager::Initialize()
{
    if (m_initialized) {
        spdlog::warn("ShackleModeManager already initialized");
        return;
    }

    if (!g_higgsInterface) {
        spdlog::error("ShackleModeManager::Initialize - HIGGS interface not available!");
        return;
    }

    // Register for trigger input
    auto* inputMgr = InputManager::GetSingleton();
    if (!inputMgr || !inputMgr->IsInitialized()) {
        spdlog::error("ShackleModeManager::Initialize - InputManager not initialized!");
        return;
    }

    // Register for trigger button events
    uint64_t triggerMask = vr::ButtonMaskFromId(vr::k_EButton_A);
    inputMgr->AddVrButtonCallback(triggerMask, [](bool isLeft, bool isReleased, vr::EVRButtonId buttonId) -> bool {
        return ShackleModeManager::GetSingleton()->OnTriggerInput(isLeft, isReleased, buttonId);
    });
    spdlog::info("ShackleModeManager: Registered trigger callback (mask: 0x{:X})", triggerMask);

    // Register pre-physics callback with HIGGS
    g_higgsInterface->AddPrePhysicsStepCallback(&ShackleModeManager::PrePhysicsStepCallback);
    spdlog::info("ShackleModeManager: Registered pre-physics step callback with HIGGS");

    m_shacklePool.Clear();

    m_initialized = true;
    spdlog::info("ShackleModeManager initialized successfully");
}

void ShackleModeManager::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    m_shacklePool.Clear();

    m_initialized = false;
    spdlog::info("ShackleModeManager shut down");
}

bool ShackleModeManager::OnTriggerInput(bool isLeft, bool isReleased, vr::EVRButtonId buttonId)
{
    const char* handName = isLeft ? "Left" : "Right";
    spdlog::info("ShackleModeManager: Button {} on {} hand",
        isReleased ? "RELEASED" : "PRESSED", handName);

    if (!g_higgsInterface) {
        spdlog::warn("ShackleModeManager: HIGGS interface not available");
        return false;
    }

    if (isReleased) {
        spdlog::info("ShackleModeManager: Button released");
        return false;
    }

    // Get what both hands are holding
    RE::TESObjectREFR* leftObj = g_higgsInterface->GetGrabbedObject(true);
    RE::NiObject* leftBody = g_higgsInterface->GetGrabbedRigidBody(true);
    RE::TESObjectREFR* rightObj = g_higgsInterface->GetGrabbedObject(false);
    RE::NiObject* rightBody = g_higgsInterface->GetGrabbedRigidBody(false);

    spdlog::info("ShackleModeManager: Left hand: obj={}, Right hand: obj={}",
        leftObj ? fmt::format("0x{:X}", leftObj->GetFormID()) : "null",
        rightObj ? fmt::format("0x{:X}", rightObj->GetFormID()) : "null");

    // Determine which hand pressed the button and which is the "target"
    RE::TESObjectREFR* buttonHandObj = isLeft ? leftObj : rightObj;
    RE::NiObject* buttonHandBody = isLeft ? leftBody : rightBody;
    RE::TESObjectREFR* otherHandObj = isLeft ? rightObj : leftObj;
    RE::NiObject* otherHandBody = isLeft ? rightBody : leftBody;

    // Need at least something in the button hand to do anything
    if (!buttonHandObj || !buttonHandBody) {
        spdlog::info("ShackleModeManager: Nothing grabbed in button hand, ignoring");
        return false;
    }

    if (!NpcUtils::IsNpcLimb(buttonHandBody, buttonHandObj)) {
        spdlog::info("ShackleModeManager: Button hand object is not an NPC limb");
        return false;
    }

    // Check for double-tap to release all shackles for this NPC
    if (m_doubleTapDetector.Detect(buttonHandObj)) {
        spdlog::info("ShackleModeManager: Double-tap detected! Releasing all shackles for NPC");
        m_shacklePool.ReleaseAllForNpc(buttonHandObj);
        m_doubleTapDetector.Reset();
        return true;  // Consume input
    }

    // Check if this limb is already shackled - toggle off
    int existingShackle = m_shacklePool.FindByRigidBody(buttonHandBody);
    if (existingShackle >= 0) {
        spdlog::info("ShackleModeManager: Limb already shackled in slot {}, releasing (toggle)", existingShackle);
        m_shacklePool.Release(existingShackle);
        return true;  // Consume input
    }

    // Find a free slot
    int freeSlot = m_shacklePool.FindFreeSlot();
    if (freeSlot < 0) {
        spdlog::warn("ShackleModeManager: No free shackle slots!");
        return false;
    }

    // Get the follower body's Havok rigid body
    auto* followerBhk = skyrim_cast<RE::bhkRigidBody*>(buttonHandBody);
    if (!followerBhk) {
        spdlog::warn("ShackleModeManager: Failed to cast follower to bhkRigidBody");
        return false;
    }
    RE::hkpRigidBody* followerHk = followerBhk->GetRigidBody();
    if (!followerHk) {
        spdlog::warn("ShackleModeManager: Follower GetRigidBody() returned null");
        return false;
    }

    // Determine shackle type based on whether the other hand has an NPC limb
    bool otherHandHasNpcLimb = otherHandObj && otherHandBody && NpcUtils::IsNpcLimb(otherHandBody, otherHandObj);

    ShackleData* shackle = m_shacklePool.Activate(freeSlot);
    if (!shackle) {
        spdlog::warn("ShackleModeManager: Failed to activate shackle slot");
        return false;
    }

    shackle->rigidBody = buttonHandBody;
    shackle->npcRef = buttonHandObj;

    if (otherHandHasNpcLimb) {
        // RELATIVE SHACKLE: Connect two limbs together
        auto* anchorBhk = skyrim_cast<RE::bhkRigidBody*>(otherHandBody);
        if (!anchorBhk) {
            spdlog::warn("ShackleModeManager: Failed to cast anchor to bhkRigidBody");
            m_shacklePool.Release(freeSlot);
            return false;
        }
        RE::hkpRigidBody* anchorHk = anchorBhk->GetRigidBody();
        if (!anchorHk) {
            spdlog::warn("ShackleModeManager: Anchor GetRigidBody() returned null");
            m_shacklePool.Release(freeSlot);
            return false;
        }

        shackle->type = ShackleType::Relative;
        shackle->anchorBody = otherHandBody;
        shackle->anchorNpcRef = otherHandObj;

        // Calculate relative offset: follower position in anchor's local space
        const RE::hkTransform& anchorTransform = anchorHk->GetMotionState()->transform;
        const RE::hkTransform& followerTransform = followerHk->GetMotionState()->transform;

        // relativePos = inverse(anchorRot) * (followerPos - anchorPos)
        // For simplicity, store world-space offset for now (will rotate by anchor each frame)
        shackle->targetPosHavok.quad.m128_f32[0] = followerTransform.translation.quad.m128_f32[0] - anchorTransform.translation.quad.m128_f32[0];
        shackle->targetPosHavok.quad.m128_f32[1] = followerTransform.translation.quad.m128_f32[1] - anchorTransform.translation.quad.m128_f32[1];
        shackle->targetPosHavok.quad.m128_f32[2] = followerTransform.translation.quad.m128_f32[2] - anchorTransform.translation.quad.m128_f32[2];
        shackle->targetPosHavok.quad.m128_f32[3] = 0.0f;

        MathUtils::RotationMatrixToQuaternion(followerTransform.rotation, shackle->targetRotHavok);

        spdlog::info("ShackleModeManager: Created RELATIVE shackle in slot {}", freeSlot);
        spdlog::info("  Anchor NPC: {} (0x{:X})", NpcUtils::GetActorName(otherHandObj), otherHandObj->GetFormID());
        spdlog::info("  Follower NPC: {} (0x{:X})", NpcUtils::GetActorName(buttonHandObj), buttonHandObj->GetFormID());
        spdlog::info("  Offset: ({:.4f}, {:.4f}, {:.4f})",
            shackle->targetPosHavok.quad.m128_f32[0],
            shackle->targetPosHavok.quad.m128_f32[1],
            shackle->targetPosHavok.quad.m128_f32[2]);
    }
    else {
        // WORLD SHACKLE: Pin to current world position
        shackle->type = ShackleType::World;
        const RE::hkTransform& currentTransform = followerHk->GetMotionState()->transform;
        shackle->targetPosHavok = currentTransform.translation;
        MathUtils::RotationMatrixToQuaternion(currentTransform.rotation, shackle->targetRotHavok);

        spdlog::info("ShackleModeManager: Created WORLD shackle in slot {}", freeSlot);
        spdlog::info("  NPC: {} (0x{:X})", NpcUtils::GetActorName(buttonHandObj), buttonHandObj->GetFormID());
        spdlog::info("  Pos: ({:.4f}, {:.4f}, {:.4f})",
            shackle->targetPosHavok.quad.m128_f32[0],
            shackle->targetPosHavok.quad.m128_f32[1],
            shackle->targetPosHavok.quad.m128_f32[2]);
    }

    spdlog::info("ShackleModeManager: Active shackle count: {}", m_shacklePool.GetActiveCount());
    return true;  // Consume input since we're holding something
}

void ShackleModeManager::PrePhysicsStepCallback(void* world)
{
    auto* instance = GetSingleton();
    if (!instance || !instance->m_initialized) {
        return;
    }

    if (instance->m_shacklePool.GetActiveCount() > 0) {
        instance->UpdateShackledLimbs(world);
    }
}

void ShackleModeManager::UpdateShackledLimbs(void* world)
{
    // Get delta time for inverse calculation
    float deltaTime = *HavokUtils::g_deltaTime;
    if (deltaTime <= 0.0f) {
        deltaTime = 1.0f / 60.0f;  // Fallback to 60fps
    }
    float invDeltaTime = 1.0f / deltaTime;

    // Process each active shackle
    for (int i = 0; i < m_shacklePool.Size(); ++i) {
        ShackleData* shackle = m_shacklePool.Get(i);
        if (!shackle || !shackle->active) {
            continue;
        }

        // Validate the follower rigid body is still valid
        if (!shackle->rigidBody) {
            spdlog::warn("Shackle {}: rigidBody became null, deactivating", i);
            m_shacklePool.Release(i);
            continue;
        }

        auto* followerBhk = skyrim_cast<RE::bhkRigidBody*>(shackle->rigidBody);
        if (!followerBhk) {
            spdlog::warn("Shackle {}: Failed to cast follower to bhkRigidBody, deactivating", i);
            m_shacklePool.Release(i);
            continue;
        }

        RE::hkpRigidBody* followerHk = followerBhk->GetRigidBody();
        if (!followerHk) {
            spdlog::warn("Shackle {}: Follower GetRigidBody() returned null, deactivating", i);
            m_shacklePool.Release(i);
            continue;
        }

        RE::hkVector4 targetPos;
        RE::hkQuaternion targetRot = shackle->targetRotHavok;

        if (shackle->type == ShackleType::World) {
            // World shackle: use stored absolute position
            targetPos = shackle->targetPosHavok;
        }
        else {
            // Relative shackle: compute position from anchor + offset
            if (!shackle->anchorBody) {
                spdlog::warn("Shackle {}: Anchor body became null, deactivating", i);
                m_shacklePool.Release(i);
                continue;
            }

            auto* anchorBhk = skyrim_cast<RE::bhkRigidBody*>(shackle->anchorBody);
            if (!anchorBhk) {
                spdlog::warn("Shackle {}: Failed to cast anchor to bhkRigidBody, deactivating", i);
                m_shacklePool.Release(i);
                continue;
            }

            RE::hkpRigidBody* anchorHk = anchorBhk->GetRigidBody();
            if (!anchorHk) {
                spdlog::warn("Shackle {}: Anchor GetRigidBody() returned null, deactivating", i);
                m_shacklePool.Release(i);
                continue;
            }

            // Get anchor's current position and add the stored offset
            const RE::hkTransform& anchorTransform = anchorHk->GetMotionState()->transform;
            targetPos.quad.m128_f32[0] = anchorTransform.translation.quad.m128_f32[0] + shackle->targetPosHavok.quad.m128_f32[0];
            targetPos.quad.m128_f32[1] = anchorTransform.translation.quad.m128_f32[1] + shackle->targetPosHavok.quad.m128_f32[1];
            targetPos.quad.m128_f32[2] = anchorTransform.translation.quad.m128_f32[2] + shackle->targetPosHavok.quad.m128_f32[2];
            targetPos.quad.m128_f32[3] = 0.0f;
        }

        HavokUtils::applyHardKeyFrame(targetPos, targetRot, invDeltaTime, followerHk);
    }
}
