#include "ClimbingDamageManager.h"
#include "ClimbManager.h"
#include "EquipmentManager.h"
#include "Config.h"
#include <spdlog/spdlog.h>

ClimbingDamageManager* ClimbingDamageManager::GetSingleton()
{
    static ClimbingDamageManager instance;
    return &instance;
}

void ClimbingDamageManager::RegisterEventSink()
{
    auto* eventHolder = RE::ScriptEventSourceHolder::GetSingleton();
    if (eventHolder) {
        eventHolder->AddEventSink<RE::TESHitEvent>(this);
        spdlog::info("ClimbingDamageManager: Registered for hit events");
    }
}

void ClimbingDamageManager::SetClimbingState(bool isClimbing)
{
    if (isClimbing && !m_isClimbing) {
        // Starting to climb - record current health
        m_lastKnownHealth = GetCurrentHealth();
    }
    m_isClimbing = isClimbing;
}

RE::BSEventNotifyControl ClimbingDamageManager::ProcessEvent(
    const RE::TESHitEvent* a_event,
    RE::BSTEventSource<RE::TESHitEvent>*)
{
    if (!a_event) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Only process if enabled and currently climbing
    if (!Config::options.climbingDamageEnabled || !m_isClimbing) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Beast forms (werewolf/vampire lord) are immune to damage-based grip release
    if (EquipmentManager::GetSingleton()->IsInBeastForm()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Check if the player is the target
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return RE::BSEventNotifyControl::kContinue;
    }

    if (a_event->target.get() != player) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Player was hit - check damage amount
    float currentHealth = GetCurrentHealth();
    float maxHealth = GetMaxHealth();

    if (maxHealth <= 0.0f) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Calculate damage as percentage of max health
    float damageTaken = m_lastKnownHealth - currentHealth;
    float damagePercent = (damageTaken / maxHealth) * 100.0f;

    // Update last known health for next hit
    m_lastKnownHealth = currentHealth;

    // Check if damage exceeds threshold
    if (damageTaken > 0.0f && damagePercent >= Config::options.damageThresholdPercent) {
        spdlog::info("ClimbingDamageManager: Player took {:.1f}% damage ({:.1f} HP) while climbing - forcing grip release!",
                     damagePercent, damageTaken);
        ForceReleaseGrips();
    }

    return RE::BSEventNotifyControl::kContinue;
}

float ClimbingDamageManager::GetCurrentHealth() const
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return 0.0f;
    }

    return player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
}

float ClimbingDamageManager::GetMaxHealth() const
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return 0.0f;
    }

    return player->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kHealth);
}

void ClimbingDamageManager::ForceReleaseGrips()
{
    auto* climbManager = ClimbManager::GetSingleton();
    if (climbManager) {
        climbManager->ForceReleaseAllGrips();
    }
}
