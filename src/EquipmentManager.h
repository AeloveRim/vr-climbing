#pragma once

#include "RE/Skyrim.h"

// Manages equipment-related queries for climbing ability calculations
class EquipmentManager
{
public:
    static EquipmentManager* GetSingleton();

    // Get total weight of all worn armor pieces (raw weight)
    float GetTotalArmorWeight() const;

    // Get total armor weight scaled by armor skills
    // Each piece's weight is reduced based on Light/Heavy Armor skill level:
    // - Skill 0-10: full weight (factor = 1.0)
    // - Skill 10-100: linear interpolation (10 -> 1.0, 100 -> 0.0)
    float GetTotalArmorWeightSkillScaled() const;

    // Check if player is over-encumbered
    bool IsOverEncumbered() const;

    // Check if player is in beast form (werewolf or vampire lord)
    bool IsInBeastForm() const;

    // Check if player is Khajiit
    bool IsKhajiit() const;

    // Check if player is Argonian
    bool IsArgonian() const;

private:
    EquipmentManager() = default;
    ~EquipmentManager() = default;
    EquipmentManager(const EquipmentManager&) = delete;
    EquipmentManager& operator=(const EquipmentManager&) = delete;
};
