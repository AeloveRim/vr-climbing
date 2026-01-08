// Not actually used in the mod. Was playing around with physics constraints

#pragma once

#include "higgsinterface001.h"
#include "InputManager.h"
#include "ShacklePool.h"
#include "DoubleTapDetector.h"

class ShackleModeManager
{
public:
    static ShackleModeManager* GetSingleton();

    void Initialize();
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }

    // Called when trigger is pressed/released
    bool OnTriggerInput(bool isLeft, bool isReleased, vr::EVRButtonId buttonId);

    // Physics callback - called before each physics step
    static void PrePhysicsStepCallback(void* world);

private:
    ShackleModeManager() = default;
    ~ShackleModeManager() = default;
    ShackleModeManager(const ShackleModeManager&) = delete;
    ShackleModeManager& operator=(const ShackleModeManager&) = delete;

    void UpdateShackledLimbs(void* world);

    static constexpr int MAX_SHACKLES = 256;
    ShacklePool<MAX_SHACKLES> m_shacklePool;
    DoubleTapDetector m_doubleTapDetector{0.4f};

    bool m_initialized = false;
};
