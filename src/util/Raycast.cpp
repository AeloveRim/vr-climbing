#include "Raycast.h"
#include <cmath>

namespace Raycast {

RaycastResult CastRay(const RE::NiPoint3& origin, const RE::NiPoint3& direction, float maxDistance) {
    RaycastResult result;
    result.hit = false;
    result.distance = maxDistance;
    result.hitPoint = origin + direction * maxDistance;
    result.hitNormal = {0.0f, 0.0f, 0.0f};
    result.collisionLayer = RE::COL_LAYER::kUnidentified;
    result.hitRef = nullptr;

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !player->parentCell) {
        return result;
    }

    auto* physicsWorld = player->parentCell->GetbhkWorld();
    if (!physicsWorld) {
        return result;
    }

    float havokWorldScale = RE::bhkWorld::GetWorldScale();

    RE::NiPoint3 rayStart = origin;
    RE::NiPoint3 rayEnd = origin + direction * maxDistance;

    RE::bhkPickData pickData;
    pickData.rayInput.from = rayStart * havokWorldScale;
    pickData.rayInput.to = rayEnd * havokWorldScale;

    // Use default collector (bhkPickData has built-in ray output)
    physicsWorld->PickObject(pickData);

    // Check if we got a hit using the default ray hit output
    if (pickData.rayOutput.HasHit()) {
        float hitFraction = pickData.rayOutput.hitFraction;
        result.hit = true;
        result.distance = maxDistance * hitFraction;
        result.hitPoint = origin + direction * result.distance;
        result.hitNormal = {
            pickData.rayOutput.normal.quad.m128_f32[0],
            pickData.rayOutput.normal.quad.m128_f32[1],
            pickData.rayOutput.normal.quad.m128_f32[2]
        };

        // Extract collision layer and hit reference from collidable
        if (pickData.rayOutput.rootCollidable) {
            result.collisionLayer = pickData.rayOutput.rootCollidable->GetCollisionLayer();
            // Try to get the TESObjectREFR from the collidable
            result.hitRef = RE::TESHavokUtilities::FindCollidableRef(*pickData.rayOutput.rootCollidable);
        }
    }

    return result;
}

RaycastResult CastRay(const RE::NiPoint3& origin, const RE::NiPoint3& direction, float maxDistance, CollisionLayerMask layerMask) {
    RaycastResult result;
    result.hit = false;
    result.distance = maxDistance;
    result.hitPoint = origin + direction * maxDistance;
    result.hitNormal = {0.0f, 0.0f, 0.0f};
    result.collisionLayer = RE::COL_LAYER::kUnidentified;
    result.hitRef = nullptr;

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !player->parentCell) {
        return result;
    }

    auto* physicsWorld = player->parentCell->GetbhkWorld();
    if (!physicsWorld) {
        return result;
    }

    float havokWorldScale = RE::bhkWorld::GetWorldScale();

    // Iteratively cast rays, skipping layers that don't match the mask
    // Each iteration starts just past the previous hit
    constexpr int MAX_ITERATIONS = 8;  // Prevent infinite loops
    constexpr float EPSILON = 0.01f;   // Small offset to avoid hitting the same object

    float accumulatedDistance = 0.0f;
    RE::NiPoint3 currentOrigin = origin;

    for (int i = 0; i < MAX_ITERATIONS; ++i) {
        float remainingDistance = maxDistance - accumulatedDistance;
        if (remainingDistance <= 0.0f) {
            break;
        }

        RE::NiPoint3 rayEnd = currentOrigin + direction * remainingDistance;

        RE::bhkPickData pickData;
        pickData.rayInput.from = currentOrigin * havokWorldScale;
        pickData.rayInput.to = rayEnd * havokWorldScale;

        physicsWorld->PickObject(pickData);

        if (!pickData.rayOutput.HasHit()) {
            // No more hits, we're done
            break;
        }

        float hitFraction = pickData.rayOutput.hitFraction;
        float hitDistance = remainingDistance * hitFraction;
        RE::NiPoint3 hitPoint = currentOrigin + direction * hitDistance;

        // Check if this hit is on a layer we care about
        RE::COL_LAYER layer = RE::COL_LAYER::kUnidentified;
        RE::TESObjectREFR* hitRef = nullptr;

        if (pickData.rayOutput.rootCollidable) {
            layer = pickData.rayOutput.rootCollidable->GetCollisionLayer();
            hitRef = RE::TESHavokUtilities::FindCollidableRef(*pickData.rayOutput.rootCollidable);
        }

        CollisionLayerMask layerBit = 1ULL << static_cast<uint64_t>(layer);
        if ((layerMask & layerBit) != 0) {
            // Found a hit on a matching layer!
            result.hit = true;
            result.distance = accumulatedDistance + hitDistance;
            result.hitPoint = hitPoint;
            result.hitNormal = {
                pickData.rayOutput.normal.quad.m128_f32[0],
                pickData.rayOutput.normal.quad.m128_f32[1],
                pickData.rayOutput.normal.quad.m128_f32[2]
            };
            result.collisionLayer = layer;
            result.hitRef = hitRef;
            return result;
        }

        // Layer doesn't match - continue past this hit
        accumulatedDistance += hitDistance + EPSILON;
        currentOrigin = hitPoint + direction * EPSILON;
    }

    return result;
}

float GetAllowedDistance(const RE::NiPoint3& origin, const RE::NiPoint3& direction, float maxDistance, float buffer) {
    RaycastResult rayResult = CastRay(origin, direction, maxDistance + buffer);

    if (rayResult.hit) {
        // Wall found, limit movement to (distance - buffer), minimum 0
        float allowed = rayResult.distance - buffer;
        return allowed > 0.0f ? allowed : 0.0f;
    }

    // No wall, allow full movement
    return maxDistance;
}

float GetAllowedDistance(const RE::NiPoint3& origin, const RE::NiPoint3& direction, float maxDistance, float buffer, LayerFilter layerFilter) {
    RaycastResult rayResult = CastRay(origin, direction, maxDistance + buffer);

    // Only consider hits on layers that pass the filter
    if (rayResult.hit && layerFilter(rayResult.collisionLayer)) {
        // Wall found, limit movement to (distance - buffer), minimum 0
        float allowed = rayResult.distance - buffer;
        return allowed > 0.0f ? allowed : 0.0f;
    }

    // No blocking wall, allow full movement
    return maxDistance;
}

} // namespace Raycast
