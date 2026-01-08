#include "EquipmentManager.h"

#include <algorithm>
#include <cstring>
#include <spdlog/spdlog.h>

EquipmentManager* EquipmentManager::GetSingleton()
{
    static EquipmentManager instance;
    return &instance;
}

float EquipmentManager::GetTotalArmorWeight() const
{
    // Beast forms have no armor weight penalty
    if (IsInBeastForm()) {
        return 0.0f;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return 0.0f;
    }

    float totalWeight = 0.0f;

    // Track already-counted armor to avoid duplicates (armor can occupy multiple slots)
    std::vector<RE::FormID> countedArmor;

    // Iterate over biped slots 30-45 (standard armor slots)
    for (int slot = 30; slot <= 45; ++slot) {
        auto slotMask = static_cast<RE::BIPED_MODEL::BipedObjectSlot>(1 << (slot - 30));
        auto* armor = player->GetWornArmor(slotMask);

        if (armor) {
            // Check if we already counted this armor piece
            RE::FormID formID = armor->GetFormID();
            bool alreadyCounted = false;
            for (RE::FormID counted : countedArmor) {
                if (counted == formID) {
                    alreadyCounted = true;
                    break;
                }
            }

            if (!alreadyCounted) {
                totalWeight += armor->weight;
                countedArmor.push_back(formID);
            }
        }
    }

    return totalWeight;
}

float EquipmentManager::GetTotalArmorWeightSkillScaled() const
{
    // Beast forms have no armor weight penalty
    if (IsInBeastForm()) {
        return 0.0f;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return 0.0f;
    }

    // Get current skill levels (including all bonuses from potions, enchantments, etc.)
    auto* avOwner = player->AsActorValueOwner();
    float lightArmorSkill = avOwner->GetActorValue(RE::ActorValue::kLightArmor);
    float heavyArmorSkill = avOwner->GetActorValue(RE::ActorValue::kHeavyArmor);

    // Helper lambda to calculate weight factor based on skill
    // 0-10: factor = 1.0 (full weight)
    // 10-100: linear interpolation (10 -> 1.0, 100 -> 0.0)
    auto calcSkillFactor = [](float skill) -> float {
        if (skill <= 10.0f) {
            return 1.0f;
        } else if (skill >= 100.0f) {
            return 0.0f;
        } else {
            // Linear interpolation: at 10 -> 1.0, at 100 -> 0.0
            return 1.0f - (skill - 10.0f) / 90.0f;
        }
    };

    float lightFactor = calcSkillFactor(lightArmorSkill);
    float heavyFactor = calcSkillFactor(heavyArmorSkill);

    float totalWeight = 0.0f;

    // Track already-counted armor to avoid duplicates (armor can occupy multiple slots)
    std::vector<RE::FormID> countedArmor;

    // Iterate over biped slots 30-45 (standard armor slots)
    for (int slot = 30; slot <= 45; ++slot) {
        auto slotMask = static_cast<RE::BIPED_MODEL::BipedObjectSlot>(1 << (slot - 30));
        auto* armor = player->GetWornArmor(slotMask);

        if (armor) {
            // Check if we already counted this armor piece
            RE::FormID formID = armor->GetFormID();
            bool alreadyCounted = false;
            for (RE::FormID counted : countedArmor) {
                if (counted == formID) {
                    alreadyCounted = true;
                    break;
                }
            }

            if (!alreadyCounted) {
                // Apply skill-based weight reduction
                float baseWeight = armor->weight;
                float factor;

                if (armor->IsLightArmor()) {
                    factor = lightFactor;
                } else if (armor->IsHeavyArmor()) {
                    factor = heavyFactor;
                } else {
                    // Clothing or other - use full weight (no skill reduction)
                    factor = 1.0f;
                }

                totalWeight += baseWeight * factor;
                countedArmor.push_back(formID);
            }
        }
    }

    return totalWeight;
}

bool EquipmentManager::IsOverEncumbered() const
{
    // Beast forms cannot be over-encumbered for climbing purposes
    if (IsInBeastForm()) {
        return false;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return false;
    }

    return player->IsOverEncumbered();
}

bool EquipmentManager::IsInBeastForm() const
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return false;
    }

    auto* race = player->GetRace();
    if (!race) {
        return false;
    }

    const char* editorID = race->GetFormEditorID();
    if (editorID) {
        if (std::strcmp(editorID, "WerewolfBeastRace") == 0 ||
            std::strcmp(editorID, "DLC1VampireBeastRace") == 0) {
            return true;
        }
    }

    return false;
}

bool EquipmentManager::IsKhajiit() const
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return false;
    }

    auto* race = player->GetRace();
    if (!race) {
        return false;
    }

    const char* editorID = race->GetFormEditorID();
    if (editorID) {
        return std::strcmp(editorID, "KhajiitRace") == 0;
    }

    return false;
}

bool EquipmentManager::IsArgonian() const
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return false;
    }

    auto* race = player->GetRace();
    if (!race) {
        return false;
    }

    const char* editorID = race->GetFormEditorID();
    if (editorID) {
        return std::strcmp(editorID, "ArgonianRace") == 0;
    }

    return false;
}
