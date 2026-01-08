#pragma once

#include "VRHookAPI.h"
#include "InputManager.h"
#include "higgsinterface001.h"
#include "RE/Skyrim.h"
#include <chrono>
#include <deque>

// Manages VR climbing mechanics
// When player grips while touching climbable surfaces, they can pull themselves around
class ClimbManager
{
public:
    static ClimbManager* GetSingleton();

    void Initialize();
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }
    bool IsClimbing() const { return m_leftGrabbing || m_rightGrabbing; }

    // Check if player is in beast form (werewolf or vampire lord)
    static bool IsPlayerInBeastForm();

    // Force release all grips (called by external systems like ClimbingDamageManager)
    void ForceReleaseAllGrips();

    // Force release all grips without starting ballistic flight (used when menu opens)
    void ForceReleaseAllGripsNoLaunch();

private:
    ClimbManager() = default;
    ~ClimbManager() = default;
    ClimbManager(const ClimbManager&) = delete;
    ClimbManager& operator=(const ClimbManager&) = delete;

    // HIGGS PrePhysicsStep callback - called before physics simulation each frame
    static void OnPrePhysicsStep(void* world);

    // Input callbacks - return true to consume input
    bool OnGripPressed(bool isLeft);
    bool OnGripReleased(bool isLeft);

    // Per-frame climbing logic (called from physics step)
    void UpdateClimbing();

    // Start/stop climbing for a specific hand
    void StartClimb(bool isLeft);
    void StopClimb(bool isLeft);

    // Apply smoothed climbing movement to player
    void ApplyClimbMovement(float deltaTime);

    // Get current hand position in world space
    RE::NiPoint3 GetHandWorldPosition(bool isLeft) const;

    // State
    bool m_initialized = false;

    // Climbing state per hand
    bool m_leftGrabbing = false;
    bool m_rightGrabbing = false;

    // Grab anchor points in world space (where hand was when grip started)
    RE::NiPoint3 m_leftGrabPoint;
    RE::NiPoint3 m_rightGrabPoint;

    // Previous hand offsets from player (not world positions!) for delta calculation
    // Using offsets ensures player movement doesn't affect the delta calculation
    RE::NiPoint3 m_leftPrevHandOffset;
    RE::NiPoint3 m_rightPrevHandOffset;

    // Saved gravity value to restore after climbing
    float m_savedGravity = 0.0f;
    bool m_gravityDisabled = false;

    // Input callback IDs
    InputManager::CallbackId m_gripCallbackId = InputManager::InvalidCallbackId;

    // Position smoothing
    RE::NiPoint3 m_targetPosition;           // Target position we're smoothing toward
    bool m_hasTargetPosition = false;        // Whether target position is valid
    std::chrono::steady_clock::time_point m_lastUpdateTime;
    bool m_hasLastUpdateTime = false;

    // Velocity tracking for launch mechanics
    struct VelocitySample {
        RE::NiPoint3 delta;      // Movement delta this frame
        float deltaTime;         // Time for this sample
    };
    std::deque<VelocitySample> m_velocityHistory;

    // Calculate launch velocity from recent movement history
    RE::NiPoint3 CalculateLaunchVelocity() const;

    // Apply launch velocity to player
    void ApplyLaunch(const RE::NiPoint3& velocity);

    // Track raw grip button state (held vs not held) for auto-catch feature
    // This lets us know if the player is holding grip when auto-catch triggers
    bool m_leftGripHeld = false;
    bool m_rightGripHeld = false;

    // Handle auto-catch: when ballistic flight ends due to surface detection under hands
    void HandleAutoCatch(uint8_t catchResult);
};
