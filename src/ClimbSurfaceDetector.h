#pragma once

#include "RE/Skyrim.h"

// Detects climbable surfaces near VR hands using short-range raycasts
// Used to determine if a grip action should initiate climbing
class ClimbSurfaceDetector
{
public:
    // Check if there's a climbable surface near the specified hand
    // Casts short rays in multiple directions from the hand position
    // Returns true if any ray hits climbable geometry within range
    static bool CanGrabSurface(bool isLeft);

    // Cast a ray in a specific direction from hand position
    // Returns true if a climbable surface is hit within effective ray length
    static bool CastRayInDirection(bool isLeft, const RE::NiPoint3& direction);

    // Check if a collision layer represents a climbable surface
    static bool IsClimbable(RE::COL_LAYER layer);

private:
    // Get hand position for raycasting
    static RE::NiPoint3 GetHandPosition(bool isLeft);

    // Internal implementation of climbable check
    static bool IsClimbableLayer(RE::COL_LAYER layer);

    // Cast rays in all directions and return true if any hit a climbable surface
    static bool CastMultiDirectionalRays(const RE::NiPoint3& origin);

    // Cast ray from hand toward HMD to detect if hand is inside a collider
    // Colliders are often larger than visible geometry, so hand may already be inside
    static bool CastRayTowardHMD(const RE::NiPoint3& handPos);
};
