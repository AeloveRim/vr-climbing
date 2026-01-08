#pragma once

#include <chrono>

// Manages HIGGS compatibility settings that need to change during climbing
// Currently handles disabling gravity gloves while climbing (to prevent pulling items)
class HiggsCompatManager
{
public:
    static HiggsCompatManager* GetSingleton();

    // Call once at startup after HIGGS interface is available
    // Reads and stores original HIGGS settings
    void Initialize();

    // Disable gravity gloves (call when climbing starts)
    void DisableGravityGloves();

    // Request restore of gravity gloves (delayed by 500ms)
    // Call when climbing ends - actual restore happens after delay if no new disable request
    void RestoreGravityGloves();

    // Call every frame to process delayed restore
    void Update();

    bool IsInitialized() const { return m_initialized; }

private:
    HiggsCompatManager() = default;
    ~HiggsCompatManager() = default;
    HiggsCompatManager(const HiggsCompatManager&) = delete;
    HiggsCompatManager& operator=(const HiggsCompatManager&) = delete;

    // Actually perform the restore (called after delay)
    void DoRestore();

    bool m_initialized = false;

    // Original HIGGS settings (stored at startup)
    double m_originalDisableGravityGloves = 0.0;  // 0 = enabled, 1 = disabled
    double m_originalEnableTwoHandedGrabbing = 1.0;  // 1 = enabled, 0 = disabled
    double m_originalEnableWeaponTwoHanding = 1.0;   // 1 = enabled, 0 = disabled
    double m_originalEnableGrip = 1.0;               // 1 = enabled, 0 = disabled

    // Track if we modified each setting
    bool m_gravityGlovesDisabledByUs = false;
    bool m_twoHandedGrabbingDisabledByUs = false;
    bool m_weaponTwoHandingDisabledByUs = false;
    bool m_gripDisabledByUs = false;

    // Delayed restore mechanism
    uint32_t m_disableRequestCounter = 0;           // Increments each time Disable is called
    bool m_restorePending = false;                   // True if restore is scheduled
    uint32_t m_restoreRequestId = 0;                 // Counter value when restore was requested
    std::chrono::steady_clock::time_point m_restoreRequestTime;

    static constexpr float RESTORE_DELAY_MS = 500.0f;
};
