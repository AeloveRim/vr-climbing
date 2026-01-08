#pragma once

#include "RE/Skyrim.h"

// Manages sound playback for VRClimbing
// Uses vanilla Skyrim footstep sounds played via BSAudioManager::BuildSoundDataFromFile
class AudioManager
{
public:
    static AudioManager* GetSingleton();

    // Play a random grip sound at player position
    // Uses grass sounds for beast forms, stone sounds for normal
    void PlayGripSound(bool isBeastForm);

    // Play a launch sound at player position
    // Uses sprint sounds for fast launches (>=150), sneak sounds for slow launches
    void PlayLaunchSound(float launchSpeed, bool isBeastForm);

    // Get effective volume (config volume * game master volume)
    float GetEffectiveVolume() const;

private:
    AudioManager() = default;
    ~AudioManager() = default;
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    // Play a sound file at player position with specified volume
    // Returns true if sound started playing
    bool PlaySoundFile(const char* path, float volume);

    // Get game's master sound volume (0-1)
    float GetGameMasterVolume() const;

    // Sound path templates (use %d for variant number)
    // Normal grip: stone sneak (01-06)
    static constexpr const char* GRIP_SOUND_NORMAL = "sound\\fx\\fst\\player\\stonesolid\\sneak\\fst_player_stonesolid_sneak_%02d.wav";
    static constexpr int GRIP_SOUND_NORMAL_MIN = 1;
    static constexpr int GRIP_SOUND_NORMAL_MAX = 6;

    // Beast form grip: grass sneak (01-06)
    static constexpr const char* GRIP_SOUND_BEAST = "sound\\fx\\fst\\player\\grass\\sneak\\fst_player_grass_sneak_%02d.wav";
    static constexpr int GRIP_SOUND_BEAST_MIN = 1;
    static constexpr int GRIP_SOUND_BEAST_MAX = 6;

    // Fast launch: stone sprint right foot (04-06)
    static constexpr const char* LAUNCH_SOUND_FAST = "sound\\fx\\fst\\player\\stonesolid\\sprint\\r\\fst_player_stonesolid_sprint_r_%02d.wav";
    static constexpr int LAUNCH_SOUND_FAST_MIN = 4;
    static constexpr int LAUNCH_SOUND_FAST_MAX = 6;

    // Velocity threshold for fast vs slow launch sounds
    static constexpr float LAUNCH_SPEED_THRESHOLD = 150.0f;

    // RNG state for sound variation
    mutable int m_lastGripVariant = 0;
    mutable int m_lastLaunchVariant = 0;
};
