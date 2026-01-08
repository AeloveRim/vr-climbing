#pragma once

#include "RE/Skyrim.h"

// Corrects player position when exiting climb mode to prevent falling through geometry.
// During climbing, the player's body can partially clip into surfaces. When releasing,
// this can cause the player to fall through the map if their feet are inside geometry.
// This class detects and corrects that by smoothly moving the player up onto the surface
// over several frames, following a Bezier curve that respects initial velocity direction.
class ClimbExitCorrector
{
public:
    static ClimbExitCorrector* GetSingleton();

    // Start correction process. Call when player releases all grips.
    // initialVelocity: the player's velocity at the moment of release (for direction)
    // Returns true if correction is needed and started, false if no correction needed.
    bool StartCorrection(const RE::NiPoint3& initialVelocity);

    // Update each frame while correcting. Call from your main update loop.
    // deltaTime: frame time in seconds
    // Returns true if still correcting, false when done.
    bool Update(float deltaTime);

    // Cancel any in-progress correction (e.g., if player grabs again)
    void Cancel();

    // Call every frame during climbing or ballistic mode to track safe positions.
    // Every 50 frames, checks if current position has enough vertical space to stand.
    // If so, stores it as a fallback position for when normal correction fails.
    void UpdateSafePositionCheck();

    // Clear the stored safe position (call when starting a new climb)
    void ClearSafePosition();

    // Public state - check this to know if correction is in progress
    bool isCorrecting = false;

private:
    ClimbExitCorrector() = default;

    // Detect if correction is needed and calculate target position
    // Returns correction amount (0 if not needed)
    float DetectCorrectionNeeded(RE::NiPoint3& outTargetPos);

    // Check available headroom at a given position
    // Returns actual headroom distance (large value if no ceiling)
    float CheckHeadroomAt(const RE::NiPoint3& position, float requiredHeight);

    // Try to find a horizontal escape route when vertical correction is blocked
    // searchDistance: how far to search horizontally (e.g., 50 or 100 units)
    // Returns true if valid escape found, sets outTargetPos to landing position
    bool FindHorizontalEscape(float searchDistance, RE::NiPoint3& outTargetPos);

    // Evaluate quadratic Bezier: B(t) = (1-t)²P0 + 2(1-t)tP1 + t²P2
    RE::NiPoint3 EvaluateBezier(float t) const;

    // Correction state
    RE::NiPoint3 m_startPos;       // P0: where we started
    RE::NiPoint3 m_controlPoint;   // P1: control point (velocity-influenced)
    RE::NiPoint3 m_targetPos;      // P2: where we want to end up
    float m_progress = 0.0f;       // 0 to 1
    float m_duration = 0.0f;       // Calculated duration based on distance

    // Safe position tracking (for fallback when headroom is insufficient)
    RE::NiPoint3 m_lastKnownSafePosition;  // Last position with enough headroom
    bool m_hasLastKnownSafePosition = false;
    int m_safePositionCheckCounter = 0;
    bool m_loggedHeightThisSession = false;  // Log player height once per session

    static constexpr int SAFE_POSITION_CHECK_INTERVAL = 50;  // Check every 50 frames
    static constexpr float MIN_STANDING_HEIGHT = 80.0f;       // Minimum fallback (crouched)
    static constexpr float HEADROOM_MARGIN = 10.0f;           // Extra clearance above head
};
