#pragma once

#include <RE/Skyrim.h>
#include "log.h"

namespace NpcUtils
{
    // Check if the grabbed object is an NPC limb
    inline bool IsNpcLimb(RE::NiObject* rigidBody, RE::TESObjectREFR* grabbedObj)
    {
        if (!grabbedObj) {
            spdlog::debug("IsNpcLimb: No grabbed object");
            return false;
        }

        // Check if the grabbed object is an Actor (NPC)
        auto* actor = grabbedObj->As<RE::Actor>();
        if (!actor) {
            spdlog::debug("IsNpcLimb: Grabbed object {} is not an Actor",
                grabbedObj->GetFormID());
            return false;
        }

        // It's an NPC!
        spdlog::info("IsNpcLimb: Grabbed object {} IS an Actor (NPC: {})",
            grabbedObj->GetFormID(),
            actor->GetName());

        // TODO: Further verify that rigidBody belongs to the actor's ragdoll
        // For now, assume if we're grabbing an actor, we're grabbing a limb
        (void)rigidBody;  // Suppress unused warning
        return true;
    }

    // Get the Actor from a reference, or nullptr if not an actor
    inline RE::Actor* GetActor(RE::TESObjectREFR* ref)
    {
        return ref ? ref->As<RE::Actor>() : nullptr;
    }

    // Get the actor's name safely
    inline const char* GetActorName(RE::TESObjectREFR* ref)
    {
        auto* actor = GetActor(ref);
        return actor ? actor->GetName() : "unknown";
    }
}
