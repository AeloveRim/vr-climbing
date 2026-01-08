#pragma once

#include "RE/Skyrim.h"

namespace VRNodes {

// Get the VR node data from PlayerCharacter (VR-specific API)
// Returns nullptr if not in VR mode
inline RE::VR_NODE_DATA* GetVRNodeData() {
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return nullptr;
    }
    return player->GetVRNodeData();
}

// Get the left VR controller node (actual tracked position)
// Returns nullptr if not in VR or node not found
inline RE::NiAVObject* GetLeftHand() {
    auto* vrData = GetVRNodeData();
    if (vrData && vrData->LeftWandNode) {
        return vrData->LeftWandNode.get();
    }
    return nullptr;
}

// Get the right VR controller node (actual tracked position)
// Returns nullptr if not in VR or node not found
inline RE::NiAVObject* GetRightHand() {
    auto* vrData = GetVRNodeData();
    if (vrData && vrData->RightWandNode) {
        return vrData->RightWandNode.get();
    }
    return nullptr;
}

// Get the HMD (head-mounted display) node
// Returns nullptr if not in VR or node not found
inline RE::NiAVObject* GetHMD() {
    auto* vrData = GetVRNodeData();
    if (vrData && vrData->UprightHmdNode) {
        return vrData->UprightHmdNode.get();
    }
    return nullptr;
}

// Get a named node from player skeleton by name
// Returns nullptr if player not available or node not found
inline RE::NiAVObject* GetPlayerNode(std::string_view nodeName) {
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return nullptr;
    }

    auto* root = player->Get3D();
    if (!root) {
        return nullptr;
    }

    return root->GetObjectByName(nodeName);
}

} // namespace VRNodes
