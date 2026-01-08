#include "ClimbExitCorrector.h"
#include "Config.h"
#include "util/VRNodes.h"
#include "util/Raycast.h"
#include <spdlog/spdlog.h>
#include <cmath>

ClimbExitCorrector* ClimbExitCorrector::GetSingleton()
{
    static ClimbExitCorrector instance;
    return &instance;
}

float ClimbExitCorrector::DetectCorrectionNeeded(RE::NiPoint3& outTargetPos)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return 0.0f;
    }

    auto* hmd = VRNodes::GetHMD();
    if (!hmd) {
        spdlog::warn("ClimbExitCorrector: No HMD node available");
        return 0.0f;
    }

    RE::NiPoint3 hmdPos = hmd->world.translate;
    RE::NiPoint3 playerPos = player->GetPosition();  // Feet position

    // Detection target: 130 units below HMD position
    constexpr float DETECTION_DEPTH = 130.0f;
    RE::NiPoint3 detectionTarget = { hmdPos.x, hmdPos.y, hmdPos.z - DETECTION_DEPTH };

    // Calculate direction from HMD to detection target (straight down)
    RE::NiPoint3 toTarget = detectionTarget - hmdPos;
    float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z);

    if (distance < 1.0f) {
        return 0.0f;
    }

    // Normalize direction
    RE::NiPoint3 direction = {
        toTarget.x / distance,
        toTarget.y / distance,
        toTarget.z / distance
    };

    // Cast ray from HMD toward detection target (130 units down)
    // Use filtered raycast to only hit solid geometry layers
    RaycastResult result = Raycast::CastRay(hmdPos, direction, distance, LayerMasks::kSolid);

    if (!result.hit) {
        // No solid geometry found
        return 0.0f;
    }

    // Hit detected - use feet position for correction amount
    float hitZ = result.hitPoint.z;
    float feetZ = playerPos.z;

    if (feetZ < hitZ) {
        float correctionAmount = hitZ - feetZ;
        outTargetPos = playerPos;
        outTargetPos.z += correctionAmount;
        return correctionAmount;
    }

    return 0.0f;
}

float ClimbExitCorrector::CheckHeadroomAt(const RE::NiPoint3& position, float requiredHeight)
{
    RE::NiPoint3 upDir = {0.0f, 0.0f, 1.0f};
    RE::NiPoint3 checkStart = position;
    checkStart.z += 1.0f;  // Small offset to avoid self-intersection

    // Use filtered raycast to only consider solid geometry
    RaycastResult headroomCheck = Raycast::CastRay(checkStart, upDir, requiredHeight + 10.0f, LayerMasks::kSolid);

    if (headroomCheck.hit) {
        return headroomCheck.distance;  // Return available headroom
    }

    return requiredHeight + 100.0f;  // No ceiling hit - plenty of room
}

bool ClimbExitCorrector::FindHorizontalEscape(float searchDistance, RE::NiPoint3& outTargetPos)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return false;
    }

    auto* hmd = VRNodes::GetHMD();
    if (!hmd) {
        return false;
    }

    RE::NiPoint3 playerPos = player->GetPosition();
    RE::NiPoint3 hmdPos = hmd->world.translate;

    // 8 directions: cardinals first (more likely to be valid), then diagonals
    constexpr int NUM_DIRECTIONS = 8;
    float directions[NUM_DIRECTIONS][2] = {
        { 1.0f,  0.0f},  // East
        {-1.0f,  0.0f},  // West
        { 0.0f,  1.0f},  // North
        { 0.0f, -1.0f},  // South
        { 0.707f,  0.707f},  // NE
        {-0.707f,  0.707f},  // NW
        { 0.707f, -0.707f},  // SE
        {-0.707f, -0.707f}   // SW
    };

    constexpr float HEADROOM_REQUIRED = 140.0f;  // Standing player height
    constexpr float GROUND_SEARCH_DEPTH = 200.0f;

    for (int i = 0; i < NUM_DIRECTIONS; i++) {
        float dirX = directions[i][0];
        float dirY = directions[i][1];

        // Calculate test position at escape distance
        RE::NiPoint3 testPos = {
            playerPos.x + dirX * searchDistance,
            playerPos.y + dirY * searchDistance,
            playerPos.z
        };

        // Check 1: Is the horizontal path clear?
        RE::NiPoint3 horDir = {dirX, dirY, 0.0f};
        RaycastResult pathCheck = Raycast::CastRay(playerPos, horDir, searchDistance, LayerMasks::kSolid);

        if (pathCheck.hit && pathCheck.distance < searchDistance - 5.0f) {
            // Path is blocked by solid geometry
            continue;
        }

        // Check 2: Find ground at the escape position (cast down from HMD height)
        RE::NiPoint3 groundCheckStart = {testPos.x, testPos.y, hmdPos.z};
        RE::NiPoint3 downDir = {0.0f, 0.0f, -1.0f};
        RaycastResult groundCheck = Raycast::CastRay(groundCheckStart, downDir, GROUND_SEARCH_DEPTH, LayerMasks::kSolid);

        if (!groundCheck.hit) {
            // No valid ground at this position
            continue;
        }

        // Set Z to ground level
        testPos.z = groundCheck.hitPoint.z;

        // Check 3: Verify headroom at the escape position
        float availableHeadroom = CheckHeadroomAt(testPos, HEADROOM_REQUIRED);

        if (availableHeadroom < HEADROOM_REQUIRED) {
            // Not enough room to stand here
            continue;
        }

        // Found a valid escape route!
        outTargetPos = testPos;
        spdlog::info("ClimbExitCorrector: Found horizontal escape at distance {:.1f}, direction ({:.2f}, {:.2f})",
                     searchDistance, dirX, dirY);
        return true;
    }

    spdlog::info("ClimbExitCorrector: No horizontal escape found at distance {:.1f}", searchDistance);
    return false;
}

bool ClimbExitCorrector::StartCorrection(const RE::NiPoint3& initialVelocity)
{
    if (isCorrecting) {
        spdlog::info(" Already correcting - skipping StartCorrection()");
        return true;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return false;
    }

    // Check if correction is needed
    RE::NiPoint3 targetPos;
    float correctionAmount = DetectCorrectionNeeded(targetPos);

    if (correctionAmount < 0.1f) {
        // No significant correction needed
        spdlog::info("ClimbExitCorrector: No correction needed (amount: {:.2f})", correctionAmount);
        return false;
    }

    // Store start position
    m_startPos = player->GetPosition();
    m_targetPos = targetPos;

    // Calculate player's current height dynamically (HMD height above feet)
    float requiredHeight = MIN_STANDING_HEIGHT + HEADROOM_MARGIN;  // Default fallback
    auto* hmd = VRNodes::GetHMD();
    if (hmd) {
        float currentPlayerHeight = hmd->world.translate.z - m_startPos.z;
        requiredHeight = (std::max)(currentPlayerHeight, MIN_STANDING_HEIGHT) + HEADROOM_MARGIN;
    }

    // Headroom check: verify there's enough space to stand at the target position
    float availableHeadroom = CheckHeadroomAt(m_targetPos, requiredHeight);

    if (availableHeadroom < requiredHeight) {
        spdlog::warn("ClimbExitCorrector: Insufficient headroom at target ({:.1f} < {:.1f})",
                     availableHeadroom, requiredHeight);

        // Fallback to last known safe position if available
        if (m_hasLastKnownSafePosition) {
            spdlog::info("ClimbExitCorrector: Falling back to last known safe position ({:.1f}, {:.1f}, {:.1f})",
                         m_lastKnownSafePosition.x, m_lastKnownSafePosition.y, m_lastKnownSafePosition.z);
            m_targetPos = m_lastKnownSafePosition;

            // Recalculate correction amount for the new target
            float dx = m_targetPos.x - m_startPos.x;
            float dy = m_targetPos.y - m_startPos.y;
            float dz = m_targetPos.z - m_startPos.z;
            correctionAmount = std::sqrt(dx * dx + dy * dy + dz * dz);
        } else {
            // No safe position available - clamp correction to available headroom as last resort
            float maxSafeCorrection = availableHeadroom - 5.0f;  // 5 unit margin
            if (maxSafeCorrection < 1.0f) {
                spdlog::error("ClimbExitCorrector: No safe position and no headroom - aborting correction");
                return false;
            }

            spdlog::warn("ClimbExitCorrector: No safe position - clamping correction to {:.1f}", maxSafeCorrection);
            m_targetPos.z = m_startPos.z + maxSafeCorrection;
            correctionAmount = maxSafeCorrection;
        }
    }

    // Calculate control point based on velocity direction
    // This makes the curve "lead" in the velocity direction before curving up
    float velMagnitude = std::sqrt(
        initialVelocity.x * initialVelocity.x +
        initialVelocity.y * initialVelocity.y +
        initialVelocity.z * initialVelocity.z
    );

    if (velMagnitude > 1.0f) {
        // Normalize velocity and scale it
        RE::NiPoint3 velDir = {
            initialVelocity.x / velMagnitude,
            initialVelocity.y / velMagnitude,
            initialVelocity.z / velMagnitude
        };

        // Control point: midpoint between start and target, pulled toward velocity direction
        float pullDistance = correctionAmount * Config::options.exitCorrectionControlPointScale;

        m_controlPoint = {
            (m_startPos.x + m_targetPos.x) * 0.5f + velDir.x * pullDistance,
            (m_startPos.y + m_targetPos.y) * 0.5f + velDir.y * pullDistance,
            (m_startPos.z + m_targetPos.z) * 0.5f + velDir.z * pullDistance
        };
    } else {
        // No significant velocity - just use midpoint (straight-ish curve)
        m_controlPoint = {
            (m_startPos.x + m_targetPos.x) * 0.5f,
            (m_startPos.y + m_targetPos.y) * 0.5f,
            (m_startPos.z + m_targetPos.z) * 0.5f
        };
    }

    // Calculate duration based on linear distance between start and target
    float dx = m_targetPos.x - m_startPos.x;
    float dy = m_targetPos.y - m_startPos.y;
    float dz = m_targetPos.z - m_startPos.z;
    float linearDistance = std::sqrt(dx * dx + dy * dy + dz * dz);
    m_duration = linearDistance * Config::options.exitCorrectionSecondsPerUnit;

    m_progress = 0.0f;
    isCorrecting = true;

    spdlog::info("ClimbExitCorrector: Starting smooth correction of {:.1f} units over {:.2f}s, vel=({:.1f}, {:.1f}, {:.1f})",
                 linearDistance, m_duration,
                 initialVelocity.x, initialVelocity.y, initialVelocity.z);

    return true;
}

RE::NiPoint3 ClimbExitCorrector::EvaluateBezier(float t) const
{
    // Quadratic Bezier: B(t) = (1-t)²P0 + 2(1-t)tP1 + t²P2
    float oneMinusT = 1.0f - t;
    float oneMinusT2 = oneMinusT * oneMinusT;
    float t2 = t * t;
    float twoOneMinusTT = 2.0f * oneMinusT * t;

    return {
        oneMinusT2 * m_startPos.x + twoOneMinusTT * m_controlPoint.x + t2 * m_targetPos.x,
        oneMinusT2 * m_startPos.y + twoOneMinusTT * m_controlPoint.y + t2 * m_targetPos.y,
        oneMinusT2 * m_startPos.z + twoOneMinusTT * m_controlPoint.z + t2 * m_targetPos.z
    };
}

bool ClimbExitCorrector::Update(float deltaTime)
{
    if (!isCorrecting) {
        return false;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        Cancel();
        return false;
    }

    // Advance progress
    m_progress += deltaTime / m_duration;

    if (m_progress >= 1.0f) {
        // Correction complete - snap to final position
        player->SetPosition(m_targetPos, true);
        isCorrecting = false;

        spdlog::info("ClimbExitCorrector: Correction complete");
        return false;
    }

    // Apply ease-out for smoother deceleration at the end: t' = 1 - (1-t)²
    float easedT = 1.0f - (1.0f - m_progress) * (1.0f - m_progress);

    // Evaluate Bezier at current progress
    RE::NiPoint3 newPos = EvaluateBezier(easedT);
    player->SetPosition(newPos, true);

    return true;
}

void ClimbExitCorrector::Cancel()
{
    if (isCorrecting) {
        spdlog::info("ClimbExitCorrector: Correction cancelled");
        isCorrecting = false;
    }
    m_progress = 0.0f;
}

void ClimbExitCorrector::UpdateSafePositionCheck()
{
    // Only check every SAFE_POSITION_CHECK_INTERVAL frames to reduce raycast overhead
    m_safePositionCheckCounter++;
    if (m_safePositionCheckCounter < SAFE_POSITION_CHECK_INTERVAL) {
        return;
    }
    m_safePositionCheckCounter = 0;

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return;
    }

    auto* hmd = VRNodes::GetHMD();
    if (!hmd) {
        return;
    }

    RE::NiPoint3 hmdPos = hmd->world.translate;
    RE::NiPoint3 playerPos = player->GetPosition();  // Feet position

    // Calculate player's current height dynamically (HMD height above feet)
    float currentPlayerHeight = hmdPos.z - playerPos.z;
    // Use the larger of current height or minimum (in case of weird HMD data)
    float requiredHeight = (std::max)(currentPlayerHeight, MIN_STANDING_HEIGHT) + HEADROOM_MARGIN;

    // Log player height on first check of each climb session
    if (!m_loggedHeightThisSession) {
        spdlog::info("ClimbExitCorrector: Player height = {:.1f} units, required headroom = {:.1f}",
                     currentPlayerHeight, requiredHeight);
        m_loggedHeightThisSession = true;
    }

    // Cast ray DOWN from HMD to find ground (only solid layers)
    RE::NiPoint3 downDir = {0.0f, 0.0f, -1.0f};
    RaycastResult groundCheck = Raycast::CastRay(hmdPos, downDir, 200.0f, LayerMasks::kSolid);

    float groundZ = playerPos.z;  // Default to current feet if no ground hit
    if (groundCheck.hit) {
        groundZ = groundCheck.hitPoint.z;
    }

    // Cast ray UP from ground level to check ceiling clearance (only solid layers)
    RE::NiPoint3 upDir = {0.0f, 0.0f, 1.0f};
    RE::NiPoint3 ceilingCheckStart = {hmdPos.x, hmdPos.y, groundZ + 1.0f};
    RaycastResult ceilingCheck = Raycast::CastRay(ceilingCheckStart, upDir, requiredHeight + 20.0f, LayerMasks::kSolid);

    float availableHeight;
    if (ceilingCheck.hit) {
        availableHeight = ceilingCheck.distance;
    } else {
        availableHeight = requiredHeight + 100.0f;  // No ceiling = plenty of room
    }

    // If there's enough room to stand, save this as a safe position
    if (availableHeight >= requiredHeight) {
        m_lastKnownSafePosition = {hmdPos.x, hmdPos.y, groundZ};
        m_hasLastKnownSafePosition = true;
    } else {
        spdlog::info("ClimbExitCorrector: Safe position check FAILED - headroom {:.1f} < required {:.1f} (keeping previous: {})",
                      availableHeight, requiredHeight, m_hasLastKnownSafePosition ? "yes" : "none");
    }
}

void ClimbExitCorrector::ClearSafePosition()
{
    m_hasLastKnownSafePosition = false;
    // Set counter to threshold-1 so the FIRST UpdateSafePositionCheck() call runs immediately
    // This ensures we capture a safe position right at climb start, not 50 frames later
    m_safePositionCheckCounter = SAFE_POSITION_CHECK_INTERVAL - 1;
    m_loggedHeightThisSession = false;  // Reset so we log height on next climb
    spdlog::info("ClimbExitCorrector: Safe position cleared (next check will run immediately)");
}
