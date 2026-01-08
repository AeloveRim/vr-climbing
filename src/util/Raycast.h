#pragma once

#include "RE/Skyrim.h"

// Layer mask for collision layer filtering
// Each bit corresponds to a COL_LAYER value (0-46)
using CollisionLayerMask = uint64_t;

// Helper to create a layer mask from a single layer
constexpr CollisionLayerMask MakeLayerMask(RE::COL_LAYER layer) {
    return 1ULL << static_cast<uint64_t>(layer);
}

// Combine multiple masks with bitwise OR
template<typename... Layers>
constexpr CollisionLayerMask MakeLayerMask(RE::COL_LAYER first, Layers... rest) {
    return MakeLayerMask(first) | MakeLayerMask(rest...);
}

// Pre-defined masks for common use cases
namespace LayerMasks {
    // Solid geometry layers - surfaces you can stand on
    constexpr CollisionLayerMask kSolid =
        MakeLayerMask(RE::COL_LAYER::kStatic) |
        MakeLayerMask(RE::COL_LAYER::kAnimStatic) |
        MakeLayerMask(RE::COL_LAYER::kTerrain) |
        MakeLayerMask(RE::COL_LAYER::kGround) |
        MakeLayerMask(RE::COL_LAYER::kTrees) |
        MakeLayerMask(RE::COL_LAYER::kProps);

    // All standard collision layers (excludes triggers, zones, etc.)
    constexpr CollisionLayerMask kPhysical = kSolid |
        MakeLayerMask(RE::COL_LAYER::kClutter) |
        MakeLayerMask(RE::COL_LAYER::kClutterLarge) |
        MakeLayerMask(RE::COL_LAYER::kDebrisSmall) |
        MakeLayerMask(RE::COL_LAYER::kDebrisLarge);
}

struct RaycastResult {
    bool hit;
    float distance;
    RE::NiPoint3 hitPoint;
    RE::NiPoint3 hitNormal;
    RE::COL_LAYER collisionLayer;  // Layer of the hit object (only valid if hit == true)
    RE::TESObjectREFR* hitRef;     // The object reference that was hit (may be nullptr)
};

namespace Raycast {
    // Layer filter function type - returns true if the layer should block movement
    using LayerFilter = bool(*)(RE::COL_LAYER);

    // Cast a ray from origin in direction, returns hit info
    // maxDistance is in game units
    RaycastResult CastRay(const RE::NiPoint3& origin, const RE::NiPoint3& direction, float maxDistance);

    // Cast a ray that only hits layers matching the given mask
    // Uses a custom collector that filters by collision layer during the raycast
    // layerMask: bitmask of acceptable layers (use LayerMasks::kSolid or MakeLayerMask())
    RaycastResult CastRay(const RE::NiPoint3& origin, const RE::NiPoint3& direction, float maxDistance, CollisionLayerMask layerMask);

    // Check if movement in a direction is blocked by geometry
    // Returns the allowed distance (clamped to maxDistance if no obstacle, or distance to wall minus buffer)
    float GetAllowedDistance(const RE::NiPoint3& origin, const RE::NiPoint3& direction, float maxDistance, float buffer);

    // Same as above, but only considers hits on layers that pass the filter
    // layerFilter should return true for layers that should block movement
    float GetAllowedDistance(const RE::NiPoint3& origin, const RE::NiPoint3& direction, float maxDistance, float buffer, LayerFilter layerFilter);
}
