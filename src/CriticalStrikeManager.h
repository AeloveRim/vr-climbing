#pragma once

#include "RE/Skyrim.h"
#include <chrono>
#include <unordered_set>

// Detects when player is launching at an enemy and triggers slow-motion
// for dramatic "superhero landing" moments.
// Also listens for hit events to ragdoll the target when struck during slow-mo.
// Disables collision with target NPCs during slow-mo flight to prevent landing on heads.
// Configuration is in Config::options (criticalCheckInterval, criticalRayDistance, etc.)
class CriticalStrikeManager : public RE::BSTEventSink<RE::TESHitEvent>
{
public:
    static CriticalStrikeManager* GetSingleton();

    // Call once during plugin load to register for hit events
    void RegisterEventSink();

    // Lifecycle events from BallisticController
    void OnLaunchStart();
    void OnLaunchEnd();

    // Called when player starts climbing (ends slow-mo immediately)
    void OnClimbStart();

    // Called every physics frame during ballistic flight
    // frameCount is used to throttle expensive checks
    void Update(uint32_t frameCount);

    // Check if slow-motion is currently active
    bool IsSlowMotionActive() const { return m_slowMotionActive; }

    // Check if NPC collision is currently disabled
    bool IsNPCCollisionDisabled() const { return m_npcCollisionDisabled; }

    // Get the actor that triggered slow-motion (valid during slow-mo)
    RE::Actor* GetTargetActor() const;

protected:
    // BSTEventSink<TESHitEvent> implementation
    RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>* a_eventSource) override;

private:
    CriticalStrikeManager() = default;
    ~CriticalStrikeManager() = default;
    CriticalStrikeManager(const CriticalStrikeManager&) = delete;
    CriticalStrikeManager& operator=(const CriticalStrikeManager&) = delete;

    // Core detection logic
    bool CheckForCriticalStrike();

    // Get normalized velocity direction from character controller
    bool GetVelocityDirection(RE::NiPoint3& outDirection) const;

    // Get HMD forward direction
    bool GetHMDForward(RE::NiPoint3& outForward) const;

    // Check if two directions are aligned within angle threshold
    bool AreDirectionsAligned(const RE::NiPoint3& dir1, const RE::NiPoint3& dir2, float maxAngleDegrees) const;

    // Initiate slow-motion effect
    void StartSlowMotion();

    // End slow-motion effect (reason is for logging)
    void EndSlowMotion(const char* reason = "unknown");

    // Ragdoll the target actor
    void RagdollTarget(RE::Actor* target);

    // NPC Collision Control - Disables character-to-character collision during slow-mo
    // Uses CHARACTER_FLAGS::kNoCharacterCollisions on player's controller
    void DisableNPCCollision();
    void RestoreNPCCollision();

    // State
    bool m_inFlight = false;
    bool m_slowMotionActive = false;
    bool m_criticalStrikeTriggered = false;  // Prevent re-triggering during same flight
    bool m_targetWasHit = false;             // Track if target was hit (extends slow-mo)
    bool m_npcCollisionDisabled = false;     // Track if NPC collision is currently disabled
    std::chrono::steady_clock::time_point m_slowMotionStartTime;
    std::chrono::steady_clock::time_point m_hitTime;  // When target was hit

    // The actor that triggered slow-motion (stored as handle for safety)
    RE::ActorHandle m_targetActorHandle;

    // Impact point for radius-based ragdoll eligibility
    RE::NiPoint3 m_impactPoint;

    // Track which actors have been ragdolled this slow-mo session (by FormID)
    std::unordered_set<RE::FormID> m_ragdolledActors;

    // Track which actors have collision disabled (for restoration)
    std::unordered_set<RE::FormID> m_collisionDisabledActors;

    // Duration constant (postLandDuration and postHitDuration are in Config::options)
    static constexpr float NO_HIT_TIMEOUT_FACTOR = 1.0f;   // Use full slowdownDuration while in flight

    // Landing delay state
    bool m_endingDueToLanding = false;
    std::chrono::steady_clock::time_point m_landingTime;
};
