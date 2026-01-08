#pragma once

#include <RE/Skyrim.h>
#include "log.h"

enum class ShackleType {
    World,      // Pinned to world position
    Relative    // Attached to another rigid body
};

struct ShackleData {
    bool active = false;
    ShackleType type = ShackleType::World;

    // The constrained body (follower in relative mode)
    RE::NiObject* rigidBody = nullptr;
    RE::TESObjectREFR* npcRef = nullptr;

    // For World shackles: absolute target position/rotation
    // For Relative shackles: offset from anchor
    RE::hkVector4 targetPosHavok;
    RE::hkQuaternion targetRotHavok;

    // For Relative shackles only
    RE::NiObject* anchorBody = nullptr;
    RE::TESObjectREFR* anchorNpcRef = nullptr;
};

template<int MaxShackles = 256>
class ShacklePool
{
public:
    int GetActiveCount() const { return m_activeCount; }

    // Find a shackle by its rigid body, returns slot index or -1
    int FindByRigidBody(RE::NiObject* rigidBody) const
    {
        if (!rigidBody) return -1;

        for (int i = 0; i < MaxShackles; ++i) {
            if (m_shackles[i].active && m_shackles[i].rigidBody == rigidBody) {
                return i;
            }
        }
        return -1;
    }

    // Find a free slot, returns slot index or -1
    int FindFreeSlot() const
    {
        for (int i = 0; i < MaxShackles; ++i) {
            if (!m_shackles[i].active) {
                return i;
            }
        }
        return -1;
    }

    // Get a shackle by index (for iteration)
    ShackleData* Get(int index)
    {
        if (index < 0 || index >= MaxShackles) return nullptr;
        return &m_shackles[index];
    }

    const ShackleData* Get(int index) const
    {
        if (index < 0 || index >= MaxShackles) return nullptr;
        return &m_shackles[index];
    }

    // Activate a shackle in a specific slot
    ShackleData* Activate(int slot)
    {
        if (slot < 0 || slot >= MaxShackles) return nullptr;
        if (m_shackles[slot].active) return nullptr;  // Already active

        m_shackles[slot].active = true;
        m_activeCount++;
        return &m_shackles[slot];
    }

    // Release a shackle by slot index
    void Release(int slotIndex)
    {
        if (slotIndex < 0 || slotIndex >= MaxShackles) return;

        ShackleData& shackle = m_shackles[slotIndex];
        if (!shackle.active) return;

        spdlog::info("ShacklePool: Releasing shackle in slot {} (type: {})",
            slotIndex, shackle.type == ShackleType::World ? "World" : "Relative");
        shackle.active = false;
        shackle.type = ShackleType::World;
        shackle.rigidBody = nullptr;
        shackle.npcRef = nullptr;
        shackle.anchorBody = nullptr;
        shackle.anchorNpcRef = nullptr;
        m_activeCount--;
        spdlog::info("ShacklePool: Shackle released. Active count: {}", m_activeCount);
    }

    // Release all shackles for a specific NPC (as follower or anchor)
    int ReleaseAllForNpc(RE::TESObjectREFR* npcRef)
    {
        if (!npcRef) return 0;

        int releasedCount = 0;
        for (int i = 0; i < MaxShackles; ++i) {
            if (m_shackles[i].active &&
                (m_shackles[i].npcRef == npcRef || m_shackles[i].anchorNpcRef == npcRef)) {
                m_shackles[i].active = false;
                m_shackles[i].type = ShackleType::World;
                m_shackles[i].rigidBody = nullptr;
                m_shackles[i].npcRef = nullptr;
                m_shackles[i].anchorBody = nullptr;
                m_shackles[i].anchorNpcRef = nullptr;
                m_activeCount--;
                releasedCount++;
            }
        }

        if (releasedCount > 0) {
            auto* actor = npcRef->As<RE::Actor>();
            spdlog::info("ShacklePool: Released {} shackles for NPC {} ({}). Active count: {}",
                releasedCount,
                npcRef->GetFormID(),
                actor ? actor->GetName() : "unknown",
                m_activeCount);
        }

        return releasedCount;
    }

    // Clear all shackles
    void Clear()
    {
        for (int i = 0; i < MaxShackles; ++i) {
            m_shackles[i] = ShackleData{};
        }
        m_activeCount = 0;
    }

    // Iteration support
    static constexpr int Size() { return MaxShackles; }

private:
    ShackleData m_shackles[MaxShackles];
    int m_activeCount = 0;
};
