#include "AudioManager.h"
#include "Config.h"
#include <spdlog/spdlog.h>
#include <random>
#include <cstdio>

AudioManager* AudioManager::GetSingleton()
{
    static AudioManager instance;
    return &instance;
}

float AudioManager::GetGameMasterVolume() const
{
    // Get the game's master sound volume from INI settings
    // Setting name: "fAudioMasterVolume:AudioMenu"
    auto* setting = RE::GetINISetting("fAudioMasterVolume:AudioMenu");
    if (setting && setting->GetType() == RE::Setting::Type::kFloat) {
        return setting->GetFloat();
    }
    return 1.0f;  // Default to full volume if setting not found
}

float AudioManager::GetEffectiveVolume() const
{
    // Combine config volume with game master volume
    return Config::options.soundVolume * GetGameMasterVolume();
}

bool AudioManager::PlaySoundFile(const char* path, float volume)
{
    if (!Config::options.soundEnabled) {
        return false;
    }

    auto* audioManager = RE::BSAudioManager::GetSingleton();
    if (!audioManager) {
        spdlog::warn("AudioManager: BSAudioManager not available");
        return false;
    }

    // Generate resource ID from file path
    RE::BSResource::ID resourceId;
    resourceId.GenerateFromPath(path);

    // Build sound data from file
    // Flags: 0x1A = standard playback flags (from Water-interactions-VR reference)
    RE::BSSoundHandle handle;
    audioManager->BuildSoundDataFromFile(handle, resourceId, 0x1A, 0);

    if (handle.IsValid()) {
        // Set volume (config volume * game master volume)
        handle.SetVolume(volume);

        // Set position to player
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            handle.SetPosition(player->GetPosition());
        }

        // Play the sound
        if (handle.Play()) {
            spdlog::trace("AudioManager: Playing sound '{}' at volume {:.2f}", path, volume);
            return true;
        }
    }

    spdlog::trace("AudioManager: Failed to play sound '{}'", path);
    return false;
}

void AudioManager::PlayGripSound(bool isBeastForm)
{
    if (!Config::options.soundEnabled) {
        return;
    }

    // Select sound template based on form
    const char* soundTemplate;
    int minVariant, maxVariant;

    if (isBeastForm) {
        soundTemplate = GRIP_SOUND_BEAST;
        minVariant = GRIP_SOUND_BEAST_MIN;
        maxVariant = GRIP_SOUND_BEAST_MAX;
    } else {
        soundTemplate = GRIP_SOUND_NORMAL;
        minVariant = GRIP_SOUND_NORMAL_MIN;
        maxVariant = GRIP_SOUND_NORMAL_MAX;
    }

    // Pick a random variant, trying to avoid repeating the last one
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(minVariant, maxVariant);

    int variant = dist(gen);
    // If we got the same as last time and there are other options, pick again
    if (variant == m_lastGripVariant && maxVariant > minVariant) {
        variant = dist(gen);
    }
    m_lastGripVariant = variant;

    // Build the path
    char soundPath[256];
    std::snprintf(soundPath, sizeof(soundPath), soundTemplate, variant);

    // Play at effective volume
    float volume = GetEffectiveVolume();
    PlaySoundFile(soundPath, volume);

    spdlog::debug("AudioManager: Grip sound (beast={}) variant {} at volume {:.2f}",
                  isBeastForm, variant, volume);
}

void AudioManager::PlayLaunchSound(float launchSpeed, bool isBeastForm)
{
    if (!Config::options.soundEnabled) {
        return;
    }

    // Select sound template based on speed
    const char* soundTemplate;
    int minVariant, maxVariant;

    if (launchSpeed >= LAUNCH_SPEED_THRESHOLD) {
        // Fast launch: use sprint sounds
        soundTemplate = LAUNCH_SOUND_FAST;
        minVariant = LAUNCH_SOUND_FAST_MIN;
        maxVariant = LAUNCH_SOUND_FAST_MAX;
    } else {
        // Slow launch: use sneak sounds (same as grip, based on form)
        if (isBeastForm) {
            soundTemplate = GRIP_SOUND_BEAST;
            minVariant = GRIP_SOUND_BEAST_MIN;
            maxVariant = GRIP_SOUND_BEAST_MAX;
        } else {
            soundTemplate = GRIP_SOUND_NORMAL;
            minVariant = GRIP_SOUND_NORMAL_MIN;
            maxVariant = GRIP_SOUND_NORMAL_MAX;
        }
    }

    // Pick a random variant, trying to avoid repeating the last one
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(minVariant, maxVariant);

    int variant = dist(gen);
    // If we got the same as last time and there are other options, pick again
    if (variant == m_lastLaunchVariant && maxVariant > minVariant) {
        variant = dist(gen);
    }
    m_lastLaunchVariant = variant;

    // Build the path
    char soundPath[256];
    std::snprintf(soundPath, sizeof(soundPath), soundTemplate, variant);

    // Play at effective volume
    float volume = GetEffectiveVolume();
    PlaySoundFile(soundPath, volume);

    spdlog::debug("AudioManager: Launch sound (speed={:.1f}, beast={}) variant {} at volume {:.2f}",
                  launchSpeed, isBeastForm, variant, volume);
}
