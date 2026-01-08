#include "MenuChecker.h"
#include <spdlog/spdlog.h>

// Menu checking utility adapted from activeragdoll
// Thanks to Shizof for this method of checking what menus are open
namespace MenuChecker
{
    // Menus that should stop input processing
    static const std::unordered_set<std::string> gameStoppingMenus = {
        "BarterMenu",
        "Book Menu",
        "Console",
        "Native UI Menu",
        "ContainerMenu",
        "Dialogue Menu",
        "Crafting Menu",
        "Credits Menu",
        "Debug Text Menu",
        "FavoritesMenu",
        "GiftMenu",
        "InventoryMenu",
        "Journal Menu",
        "Kinect Menu",
        "Loading Menu",
        "Lockpicking Menu",
        "MagicMenu",
        "Main Menu",
        "MapMarkerText3D",
        "MapMenu",
        "MessageBoxMenu",
        "Mist Menu",
        "Quantity Menu",
        "RaceSex Menu",
        "Sleep/Wait Menu",
        "StatsMenuSkillRing",
        "StatsMenuPerks",
        "Training Menu",
        "Tutorial Menu",
        "TweenMenu"
    };

    // Currently open menus (only modified from UI thread)
    static std::unordered_set<std::string> openMenus;

    // Thread-safe flag for game-stopped state (written from UI thread, read from physics thread)
    static std::atomic<bool> g_isGameStopped{false};

    MenuEventHandler* MenuEventHandler::GetSingleton()
    {
        static MenuEventHandler instance;
        return &instance;
    }

    RE::BSEventNotifyControl MenuEventHandler::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
    {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        std::string menuName = a_event->menuName.c_str();

        if (a_event->opening) {
            openMenus.insert(menuName);
        } else {
            openMenus.erase(menuName);
        }

        // Update atomic flag for thread-safe access from physics thread
        bool stopped = false;
        for (const auto& menu : openMenus) {
            if (gameStoppingMenus.contains(menu)) {
                stopped = true;
                break;
            }
        }
        g_isGameStopped.store(stopped, std::memory_order_release);

        return RE::BSEventNotifyControl::kContinue;
    }

    void RegisterEventSink()
    {
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink(MenuEventHandler::GetSingleton());
            spdlog::info("MenuChecker: Registered menu event sink");
        } else {
            spdlog::error("MenuChecker: Failed to get UI singleton");
        }
    }

    bool IsGameStopped()
    {
        // Thread-safe read - can be called from physics thread
        return g_isGameStopped.load(std::memory_order_acquire);
    }

    bool IsMenuOpen(const std::string& menuName)
    {
        return openMenus.contains(menuName);
    }
}
