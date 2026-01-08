#include "CriticalStrikeManager.h"
#include "Config.h"
#include "util/VRNodes.h"
#include "util/Raycast.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>  // for std::min, std::max

CriticalStrikeManager* CriticalStrikeManager::GetSingleton()
{
    static CriticalStrikeManager instance;
    return &instance;
}

void CriticalStrikeManager::RegisterEventSink()
{
    auto* eventHolder = RE::ScriptEventSourceHolder::GetSingleton();
    if (eventHolder) {
        eventHolder->AddEventSink<RE::TESHitEvent>(this);
        spdlog::info("CriticalStrikeManager: Registered for hit events");
    }
}

RE::Actor* CriticalStrikeManager::GetTargetActor() const
{
    return m_targetActorHandle.get().get();
}

void CriticalStrikeManager::OnLaunchStart()
{
    m_inFlight = true;
    m_criticalStrikeTriggered = false;  // Reset for new flight
    spdlog::debug("CriticalStrikeManager: Launch started, monitoring for critical strike");
}

void CriticalStrikeManager::OnLaunchEnd()
{
    m_inFlight = false;

    // Start landing delay for slow-mo if enabled and target wasn't hit
    if (m_slowMotionActive && Config::options.criticalEndOnLand && !m_targetWasHit) {
        spdlog::info("CriticalStrikeManager: Landing without hitting target - ending slow-mo in {:.1f}s", Config::options.postLandDuration);
        m_endingDueToLanding = true;
        m_landingTime = std::chrono::steady_clock::now();
    }
}

void CriticalStrikeManager::OnClimbStart()
{
    // Immediately end slow-mo when player grabs to climb
    if (m_slowMotionActive) {
        EndSlowMotion("climb started");
    }
}

void CriticalStrikeManager::Update(uint32_t frameCount)
{
    // Check if slow-motion timer has expired
    if (m_slowMotionActive) {
        auto now = std::chrono::steady_clock::now();
        float totalElapsed = std::chrono::duration<float>(now - m_slowMotionStartTime).count();

        const char* endReason = nullptr;
        const char* currentState = "unknown";
        float stateElapsed = 0.0f;
        float stateTimeout = 0.0f;

        if (m_targetWasHit) {
            // Target was hit - this takes priority over landing timeout
            // Clear landing flag since hit extends slow-mo beyond landing delay
            m_endingDueToLanding = false;

            currentState = "POST_HIT";
            stateElapsed = std::chrono::duration<float>(now - m_hitTime).count();
            stateTimeout = Config::options.postHitDuration;

            if (stateElapsed >= stateTimeout) {
                endReason = "post-hit timeout";
            }
        } else if (m_endingDueToLanding) {
            // Landed but no hit yet - keep slow-mo for postLandDuration real seconds
            currentState = "POST_LAND";
            stateElapsed = std::chrono::duration<float>(now - m_landingTime).count();
            stateTimeout = Config::options.postLandDuration;

            if (stateElapsed >= stateTimeout) {
                endReason = "post-land timeout";
            }
        } else {
            // Still in flight, target not hit yet - use reduced timeout
            currentState = "IN_FLIGHT";
            stateElapsed = totalElapsed;
            stateTimeout = Config::options.slowdownDuration * NO_HIT_TIMEOUT_FACTOR;

            if (stateElapsed >= stateTimeout) {
                endReason = "no-hit timeout";
            }
        }

        if (endReason) {
            EndSlowMotion(endReason);
        }
    }

    // Only check for critical strike during flight
    if (!m_inFlight) {
        return;
    }

    // Already triggered this flight - don't check again
    if (m_criticalStrikeTriggered) {
        return;
    }

    // Throttle checks to every N frames
    if (frameCount % static_cast<uint32_t>(Config::options.criticalCheckInterval) != 0) {
        return;
    }

    // Perform the detection check
    if (CheckForCriticalStrike()) {
        m_criticalStrikeTriggered = true;
        StartSlowMotion();
    }
}

bool CriticalStrikeManager::CheckForCriticalStrike()
{
    // Check if critical strike system is enabled
    if (!Config::options.criticalStrikeEnabled) {
        return false;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return false;
    }

    // Check 1: Player must have weapon drawn
    auto* actorState = player->AsActorState();
    if (!actorState || !actorState->IsWeaponDrawn()) {
        return false;
    }

    // Check 2: Get velocity and calculate speed
    auto* controller = player->GetCharController();
    if (!controller) {
        return false;
    }

    RE::hkVector4 hkVelocity;
    controller->GetLinearVelocityImpl(hkVelocity);

    float vx = hkVelocity.quad.m128_f32[0];
    float vy = hkVelocity.quad.m128_f32[1];
    float vz = hkVelocity.quad.m128_f32[2];
    float speed = std::sqrt(vx * vx + vy * vy + vz * vz);

    // Check 2a: Speed must meet minimum threshold
    if (speed < Config::options.criticalMinSpeed) {
        return false;
    }

    // Check 2b: Calculate dive angle (angle below horizontal)
    // Negative vz means moving downward
    // diveAngle = asin(-vz / speed) gives angle below horizontal
    // 0 = horizontal, 90 = straight down
    float diveAngle = 0.0f;
    if (speed > 0.001f) {
        // Only count downward motion (negative vz)
        if (vz < 0.0f) {
            float sinAngle = -vz / speed;
            sinAngle = (std::max)(-1.0f, (std::min)(1.0f, sinAngle));  // Clamp for asin
            diveAngle = std::asin(sinAngle) * (180.0f / 3.14159265f);
        }
        // If vz >= 0 (moving up or horizontal), diveAngle stays 0
    }

    if (diveAngle < Config::options.criticalMinDiveAngle) {
        return false;
    }

    // Normalize velocity for direction checks
    RE::NiPoint3 velocityDir;
    velocityDir.x = vx / speed;
    velocityDir.y = vy / speed;
    velocityDir.z = vz / speed;

    // Check 3: HMD must be roughly aligned with movement direction (if enabled)
    if (Config::options.criticalAngleCheckEnabled) {
        RE::NiPoint3 hmdForward;
        if (!GetHMDForward(hmdForward)) {
            return false;
        }

        if (!AreDirectionsAligned(velocityDir, hmdForward, Config::options.criticalHmdAlignmentAngle)) {
            return false;
        }
    }

    // Check 5: Cast ray in velocity direction to find impact point
    RE::NiPoint3 playerPos = player->GetPosition();
    playerPos.z += 50.0f;  // Offset to chest height

    RaycastResult rayResult = Raycast::CastRay(playerPos, velocityDir, Config::options.criticalRayDistance);

    if (!rayResult.hit) {
        return false;  // No impact point - nothing to land on
    }

    // Check 6: Find actors within detection radius of the IMPACT POINT
    RE::NiPoint3 impactPoint = rayResult.hitPoint;
    const float detectionRadius = Config::options.criticalDetectionRadius;
    const float detectionRadiusSq = detectionRadius * detectionRadius;

    RE::Actor* closestTarget = nullptr;
    float closestDistSq = detectionRadiusSq;

    auto* processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) {
        return false;
    }

    // Iterate through high-process actors (nearby, active actors)
    processLists->ForEachHighActor([&](RE::Actor* actor) -> RE::BSContainer::ForEachResult {
        if (!actor || actor == player) {
            return RE::BSContainer::ForEachResult::kContinue;
        }

        // Skip dead actors
        if (actor->IsDead()) {
            return RE::BSContainer::ForEachResult::kContinue;
        }

        // Skip non-hostile actors if hostilesOnly is enabled
        if (Config::options.criticalHostilesOnly && !actor->IsHostileToActor(player)) {
            return RE::BSContainer::ForEachResult::kContinue;
        }

        // Check distance from IMPACT POINT (not player)
        RE::NiPoint3 actorPos = actor->GetPosition();
        RE::NiPoint3 toActor = actorPos - impactPoint;
        float distSq = toActor.x * toActor.x + toActor.y * toActor.y + toActor.z * toActor.z;

        if (distSq > detectionRadiusSq) {
            return RE::BSContainer::ForEachResult::kContinue;
        }

        // This actor is a valid target - track closest to impact point
        if (distSq < closestDistSq) {
            closestDistSq = distSq;
            closestTarget = actor;
        }

        return RE::BSContainer::ForEachResult::kContinue;
    });

    if (!closestTarget) {
        return false;
    }

    // Store the target actor and impact point for ragdoll on hit
    m_targetActorHandle = closestTarget->GetHandle();
    m_impactPoint = impactPoint;

    float closestDist = std::sqrt(closestDistSq);
    spdlog::info("CriticalStrikeManager: Critical strike detected! Target '{}' (speed: {:.0f}, dive: {:.1f}°, dist: {:.1f}, ray: {:.1f})",
                 closestTarget->GetName(),
                 speed, diveAngle,
                 closestDist,
                 rayResult.distance);

    return true;
}

bool CriticalStrikeManager::GetVelocityDirection(RE::NiPoint3& outDirection) const
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return false;
    }

    auto* controller = player->GetCharController();
    if (!controller) {
        return false;
    }

    // Get velocity from character controller
    RE::hkVector4 hkVelocity;
    controller->GetLinearVelocityImpl(hkVelocity);

    // Convert to NiPoint3
    outDirection.x = hkVelocity.quad.m128_f32[0];
    outDirection.y = hkVelocity.quad.m128_f32[1];
    outDirection.z = hkVelocity.quad.m128_f32[2];

    // Normalize (but keep horizontal for direction check - we don't care about vertical component as much)
    float length = std::sqrt(outDirection.x * outDirection.x +
                            outDirection.y * outDirection.y +
                            outDirection.z * outDirection.z);

    if (length < 0.001f) {
        return false;  // No significant velocity
    }

    outDirection.x /= length;
    outDirection.y /= length;
    outDirection.z /= length;

    return true;
}

bool CriticalStrikeManager::GetHMDForward(RE::NiPoint3& outForward) const
{
    auto* hmd = VRNodes::GetHMD();
    if (!hmd) {
        return false;
    }

    // Get the forward direction from HMD's rotation matrix
    // The forward vector is typically the Y-axis in Skyrim's coordinate system
    const auto& rotation = hmd->world.rotate;

    // Extract forward vector (Y-axis) from rotation matrix
    outForward.x = rotation.entry[0][1];
    outForward.y = rotation.entry[1][1];
    outForward.z = rotation.entry[2][1];

    // Normalize just in case
    float length = std::sqrt(outForward.x * outForward.x +
                            outForward.y * outForward.y +
                            outForward.z * outForward.z);

    if (length < 0.001f) {
        return false;
    }

    outForward.x /= length;
    outForward.y /= length;
    outForward.z /= length;

    return true;
}

bool CriticalStrikeManager::AreDirectionsAligned(const RE::NiPoint3& dir1, const RE::NiPoint3& dir2, float maxAngleDegrees) const
{
    // Dot product gives cos(angle) between normalized vectors
    float dot = dir1.x * dir2.x + dir1.y * dir2.y + dir1.z * dir2.z;

    // Clamp to valid range for acos (parentheses prevent Windows min/max macro expansion)
    dot = (std::max)(-1.0f, (std::min)(1.0f, dot));

    // Convert to angle in degrees
    float angleDegrees = std::acos(dot) * (180.0f / 3.14159265f);

    return angleDegrees <= maxAngleDegrees;
}

void CriticalStrikeManager::StartSlowMotion()
{
    auto* vats = RE::VATS::GetSingleton();
    if (!vats) {
        spdlog::warn("CriticalStrikeManager: Failed to get VATS singleton for slow-motion");
        return;
    }

    vats->SetMagicTimeSlowdown(Config::options.worldSlowdown, Config::options.playerSlowdown);
    m_slowMotionActive = true;
    m_ragdolledActors.clear();      // Reset ragdolled actors for new slow-mo
    m_targetWasHit = false;         // Reset hit flag for new slow-mo
    m_endingDueToLanding = false;   // Reset landing flag for new slow-mo
    m_slowMotionStartTime = std::chrono::steady_clock::now();

    // Disable collision with nearby NPCs so player falls through, not on their heads
    DisableNPCCollision();

    // All durations are in real-time seconds
    float noHitTimeout = Config::options.slowdownDuration * NO_HIT_TIMEOUT_FACTOR;
    spdlog::info("=== SLOW-MO: START === world: {:.0f}%, timeouts: no-hit {:.1f}s, post-land +{:.1f}s, post-hit +{:.1f}s",
                 Config::options.worldSlowdown * 100.0f,
                 noHitTimeout,
                 Config::options.postLandDuration,
                 Config::options.postHitDuration);
}

void CriticalStrikeManager::EndSlowMotion(const char* reason)
{
    // Restore NPC collision FIRST before ending slow-mo
    RestoreNPCCollision();

    auto* vats = RE::VATS::GetSingleton();
    if (vats) {
        // Reset to normal time
        vats->SetMagicTimeSlowdown(1.0f, 1.0f);
    }

    // Calculate total duration
    float totalDuration = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_slowMotionStartTime).count();

    size_t ragdollCount = m_ragdolledActors.size();
    m_slowMotionActive = false;
    m_endingDueToLanding = false;  // Clear landing flag
    m_targetActorHandle.reset();   // Clear target reference
    m_ragdolledActors.clear();     // Clear ragdolled actors
    spdlog::info("=== SLOW-MO: END ({}) === total duration: {:.2f}s real, hit: {}, ragdolled: {}",
                 reason ? reason : "unknown",
                 totalDuration,
                 m_targetWasHit ? "yes" : "no",
                 ragdollCount);
}

RE::BSEventNotifyControl CriticalStrikeManager::ProcessEvent(
    const RE::TESHitEvent* a_event,
    RE::BSTEventSource<RE::TESHitEvent>*)
{
    if (!a_event) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Only process if slow-motion is active and ragdoll is enabled
    if (!m_slowMotionActive || !Config::options.ragdollOnHit) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Get player
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Check if the player is the one dealing the hit
    if (a_event->cause.get() != player) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Get the target that was hit
    auto* hitTarget = a_event->target.get();
    if (!hitTarget) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* hitActor = hitTarget->As<RE::Actor>();
    if (!hitActor) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Check if this actor was already ragdolled this slow-mo session
    RE::FormID actorFormID = hitActor->GetFormID();
    if (m_ragdolledActors.contains(actorFormID)) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Check if actor is within ragdoll radius of impact point
    RE::NiPoint3 actorPos = hitActor->GetPosition();
    RE::NiPoint3 toActor = actorPos - m_impactPoint;
    float distSq = toActor.x * toActor.x + toActor.y * toActor.y + toActor.z * toActor.z;
    float radiusSq = Config::options.ragdollRadius * Config::options.ragdollRadius;

    if (distSq > radiusSq) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Skip non-hostile actors if hostilesOnly is enabled
    if (Config::options.criticalHostilesOnly && !hitActor->IsHostileToActor(player)) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Ragdoll the target!
    float dist = std::sqrt(distSq);
    spdlog::info("CriticalStrikeManager: Hit detected on '{}' during slow-mo (dist: {:.1f}), ragdolling!",
                 hitActor->GetName(), dist);

    RagdollTarget(hitActor);
    m_ragdolledActors.insert(actorFormID);

    // Track first hit for timing purposes
    if (!m_targetWasHit) {
        m_targetWasHit = true;
        m_hitTime = std::chrono::steady_clock::now();  // Start the post-hit timer
    }

    return RE::BSEventNotifyControl::kContinue;
}

void CriticalStrikeManager::RagdollTarget(RE::Actor* target)
{
    if (!target) {
        return;
    }

    // Check if actor is already ragdolled
    if (target->IsInRagdollState()) {
        spdlog::debug("CriticalStrikeManager: Target already ragdolled");
        return;
    }

    // Get the actor's AI process
    auto* process = target->GetActorRuntimeData().currentProcess;
    if (!process) {
        spdlog::warn("CriticalStrikeManager: Target has no AI process");
        return;
    }

    // Get player position for knockback direction
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return;
    }

    // Use KnockExplosion to ragdoll the actor
    // This is the same method used by explosions to knock actors down
    process->KnockExplosion(target, player->GetPosition(), Config::options.ragdollMagnitude);

    spdlog::info("CriticalStrikeManager: Ragdolled target '{}' with magnitude {:.1f}",
                 target->GetName(), Config::options.ragdollMagnitude);
}

void CriticalStrikeManager::DisableNPCCollision()
{
    if (!Config::options.disableNPCCollision) {
        return;  // Feature disabled in config
    }

    if (m_npcCollisionDisabled) {
        return;  // Already disabled
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return;
    }

    auto* processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) {
        return;
    }

    // Clear any previous state
    m_collisionDisabledActors.clear();

    const float collisionRadius = Config::options.ragdollRadius;
    const float collisionRadiusSq = collisionRadius * collisionRadius;

    // Disable collision on nearby NPCs using SetCollision(false)
    processLists->ForEachHighActor([&](RE::Actor* actor) -> RE::BSContainer::ForEachResult {
        if (!actor || actor == player || actor->IsDead()) {
            return RE::BSContainer::ForEachResult::kContinue;
        }

        // Check distance from impact point
        RE::NiPoint3 actorPos = actor->GetPosition();
        RE::NiPoint3 toActor = actorPos - m_impactPoint;
        float distSq = toActor.x * toActor.x + toActor.y * toActor.y + toActor.z * toActor.z;

        if (distSq > collisionRadiusSq) {
            return RE::BSContainer::ForEachResult::kContinue;
        }

        // Disable collision on this actor
        actor->SetCollision(false);
        m_collisionDisabledActors.insert(actor->GetFormID());

        return RE::BSContainer::ForEachResult::kContinue;
    });

    if (!m_collisionDisabledActors.empty()) {
        m_npcCollisionDisabled = true;
        spdlog::info("CriticalStrikeManager: Disabled collision on {} NPCs via SetCollision(false)",
                     m_collisionDisabledActors.size());
    }
}

void CriticalStrikeManager::RestoreNPCCollision()
{
    if (!m_npcCollisionDisabled || m_collisionDisabledActors.empty()) {
        m_npcCollisionDisabled = false;
        m_collisionDisabledActors.clear();
        return;
    }

    size_t restoredCount = 0;

    // Restore collision on all modified actors
    for (RE::FormID formID : m_collisionDisabledActors) {
        auto* form = RE::TESForm::LookupByID(formID);
        if (!form) {
            continue;
        }

        auto* actor = form->As<RE::Actor>();
        if (!actor) {
            continue;
        }

        // Re-enable collision
        actor->SetCollision(true);
        restoredCount++;
    }

    spdlog::info("CriticalStrikeManager: Restored collision on {} NPCs", restoredCount);

    m_collisionDisabledActors.clear();
    m_npcCollisionDisabled = false;
}
