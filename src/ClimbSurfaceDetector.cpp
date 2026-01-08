#include "ClimbSurfaceDetector.h"
#include "ClimbManager.h"
#include "BallisticController.h"
#include "Config.h"
#include "util/VRNodes.h"
#include "util/Raycast.h"
#include <spdlog/spdlog.h>
#include <cmath>

// Get effective ray length - uses config values, extended during ballistic flight
static float GetEffectiveRayLength()
{
    float length;

    // Beast forms use separate config value
    if (ClimbManager::IsPlayerInBeastForm()) {
        length = Config::options.beastGrabRayLength;
    } else {
        length = Config::options.grabRayLength;
    }

    // Double during ballistic flight for easier mid-air grabs
    if (BallisticController::GetSingleton()->IsInFlight()) {
        length *= 2.0f;
    }

    return length;
}

bool ClimbSurfaceDetector::CanGrabSurface(bool isLeft)
{
    RE::NiPoint3 handPos = GetHandPosition(isLeft);

    // Check if we got a valid position
    if (handPos.x == 0.0f && handPos.y == 0.0f && handPos.z == 0.0f) {
        return false;
    }

    // First try the 6 cardinal directions
    if (CastMultiDirectionalRays(handPos)) {
        return true;
    }

    // If no hit, try casting from hand toward HMD
    // This catches the case where hand is already inside a collider
    // (colliders are often larger than visible geometry)
    return CastRayTowardHMD(handPos);
}

RE::NiPoint3 ClimbSurfaceDetector::GetHandPosition(bool isLeft)
{
    RE::NiAVObject* handNode = isLeft ? VRNodes::GetLeftHand() : VRNodes::GetRightHand();

    if (handNode) {
        return handNode->world.translate;
    }

    return RE::NiPoint3{0.0f, 0.0f, 0.0f};
}

bool ClimbSurfaceDetector::IsClimbableLayer(RE::COL_LAYER layer)
{
    switch (layer) {
        case RE::COL_LAYER::kStatic:      // Static world geometry (walls, buildings)
        case RE::COL_LAYER::kAnimStatic:  // Animated statics (moving platforms, gates, drawbridges)
        case RE::COL_LAYER::kTerrain:     // Landscape/terrain
        case RE::COL_LAYER::kGround:      // Ground plane
        case RE::COL_LAYER::kTrees:       // Trees
        case RE::COL_LAYER::kProps:       // Larger props (furniture, etc.)
            return true;
        default:
            return false;
    }
}

bool ClimbSurfaceDetector::IsClimbable(RE::COL_LAYER layer)
{
    return IsClimbableLayer(layer);
}

bool ClimbSurfaceDetector::CastMultiDirectionalRays(const RE::NiPoint3& origin)
{
    // 6 cardinal directions: +X, -X, +Y, -Y, +Z, -Z
    static const RE::NiPoint3 directions[] = {
        { 1.0f,  0.0f,  0.0f},  // +X (right)
        {-1.0f,  0.0f,  0.0f},  // -X (left)
        { 0.0f,  1.0f,  0.0f},  // +Y (forward)
        { 0.0f, -1.0f,  0.0f},  // -Y (backward)
        { 0.0f,  0.0f,  1.0f},  // +Z (up)
        { 0.0f,  0.0f, -1.0f},  // -Z (down)
    };

    float rayLength = GetEffectiveRayLength();

    for (const auto& dir : directions) {
        RaycastResult result = Raycast::CastRay(origin, dir, rayLength);
        if (result.hit && IsClimbableLayer(result.collisionLayer)) {
            spdlog::trace("ClimbSurfaceDetector: Hit climbable surface (layer {}) at distance {} (rayLen: {})",
                          static_cast<int>(result.collisionLayer), result.distance, rayLength);
            return true;
        }
    }

    return false;
}

bool ClimbSurfaceDetector::CastRayInDirection(bool isLeft, const RE::NiPoint3& direction)
{
    RE::NiPoint3 handPos = GetHandPosition(isLeft);

    // Check if we got a valid position
    if (handPos.x == 0.0f && handPos.y == 0.0f && handPos.z == 0.0f) {
        return false;
    }

    float rayLength = GetEffectiveRayLength();

    RaycastResult result = Raycast::CastRay(handPos, direction, rayLength);
    if (result.hit && IsClimbableLayer(result.collisionLayer)) {
        return true;
    }

    return false;
}

bool ClimbSurfaceDetector::CastRayTowardHMD(const RE::NiPoint3& handPos)
{
    // Get HMD position
    RE::NiAVObject* hmdNode = VRNodes::GetHMD();
    if (!hmdNode) {
        return false;
    }

    RE::NiPoint3 hmdPos = hmdNode->world.translate;

    // Calculate direction from HMD to hand (reversed from original)
    RE::NiPoint3 toHand = {
        handPos.x - hmdPos.x,
        handPos.y - hmdPos.y,
        handPos.z - hmdPos.z
    };

    // Get distance from HMD to hand
    float distance = std::sqrt(toHand.x * toHand.x + toHand.y * toHand.y + toHand.z * toHand.z);
    if (distance < 0.001f) {
        return false;  // Hand and HMD at same position (shouldn't happen)
    }

    // Normalize direction
    RE::NiPoint3 direction = {
        toHand.x / distance,
        toHand.y / distance,
        toHand.z / distance
    };

    // Cast ray FROM HMD TOWARD hand for the full distance
    // If we hit a climbable surface before reaching the hand, that surface is
    // between the player's view and their hand = hand is at/near/inside it = valid grab!
    // (Casting from inside a collider doesn't detect hits, so we cast from outside)
    RaycastResult result = Raycast::CastRay(hmdPos, direction, distance);

    if (result.hit && IsClimbableLayer(result.collisionLayer)) {
        return true;
    }

    return false;
}
