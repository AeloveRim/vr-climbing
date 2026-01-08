#pragma once

#include "RE/Skyrim.h"

// Monitors player damage while climbing and forces grip release on significant hits
// This creates a risk/reward dynamic - taking damage while climbing is dangerous
// Configuration is in Config::options (climbingDamageEnabled, damageThresholdPercent)
class ClimbingDamageManager : public RE::BSTEventSink<RE::TESHitEvent>
{
public:
    static ClimbingDamageManager* GetSingleton();

    // Call once during plugin load to register for hit events
    void RegisterEventSink();

    // Called by ClimbManager to update climbing state
    void SetClimbingState(bool isClimbing);

    // Check if currently climbing (for event processing)
    bool IsClimbing() const { return m_isClimbing; }

protected:
    // BSTEventSink<TESHitEvent> implementation
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESHitEvent* a_event,
        RE::BSTEventSource<RE::TESHitEvent>* a_eventSource) override;

private:
    ClimbingDamageManager() = default;
    ~ClimbingDamageManager() = default;
    ClimbingDamageManager(const ClimbingDamageManager&) = delete;
    ClimbingDamageManager& operator=(const ClimbingDamageManager&) = delete;

    // Get player's current and max health
    float GetCurrentHealth() const;
    float GetMaxHealth() const;

    // Force release of all grips via ClimbManager
    void ForceReleaseGrips();

    // State
    bool m_isClimbing = false;
    float m_lastKnownHealth = 0.0f;  // Track health to detect damage amount
};
