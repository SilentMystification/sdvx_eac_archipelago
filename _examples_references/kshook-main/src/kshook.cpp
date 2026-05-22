#include <fstream>
#include <MinHook.h>
#include <gamelist.hpp>
#include <buildinfo.hpp>
#include <nlohmann/json.hpp>
#include "core/log.hpp"
#include "core/http.hpp"
#include "core/service.hpp"

auto active_game = std::unique_ptr<kshook::game>{};

namespace kshook
{
    bool init(HMODULE dll_instance)
    {
        auto name = std::string(MAX_PATH, 0);
        auto size = GetModuleFileNameA(dll_instance, name.data(), name.size());

        if (GetModuleFileNameA(dll_instance, name.data(), name.size()) == NULL)
            return false;

        const auto is_debug = (name.substr(0, size).ends_with("_debug.dll") || IS_DEBUG_BUILD);
        const auto basedir = std::filesystem::path(name).remove_filename();

        log::init(basedir / "kshook.log", is_debug);
        log::info("{} starting up.. (loaded at {:#x})", KSHOOK_USER_AGENT, std::uintptr_t(dll_instance));

        MH_Initialize();

        if (!service::init(basedir))
            log::warn("Unable to find any valid service files. No requests will be made.");

        http::init();

        auto config_file = std::ifstream(basedir / "kshook.json");
        auto config = nlohmann::json::parse(config_file, nullptr, false);

        until(games, [&]<class game_type>() {
            auto game = std::make_unique<game_type>();

            try
            {
                log::debug("Attempting to match game '{}'..", game->get_name());

                auto init_config = nlohmann::json{};

                if (!config.is_discarded() && config[game->get_id()].is_object())
                    init_config = config.at(game->get_id());

                if (!game->is_match(init_config))
                    return false;

                log::info("Running startup sequence for game '{}' ({})..", game->get_name(), game->get_id());

                if (!game->init())
                    return true;

                active_game = std::move(game);

                return true;
            }
            catch (std::exception &error)
            {
                log::error("Game '{}' initialisation failed: {}", game->get_id(), error.what());
                return false;
            }
        });

        if (active_game == nullptr)
            log::error("Unable to detect a supported game.");

        return (active_game != nullptr);
    }
}

BOOL APIENTRY DllMain(HMODULE dll_instance, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;

    DisableThreadLibraryCalls(dll_instance);

    CreateThread(
        nullptr, 0,
        [](LPVOID param) -> DWORD {
            auto dll_instance = reinterpret_cast<HMODULE>(param);

            if (kshook::init(dll_instance))
                return EXIT_SUCCESS;

            kshook::log::shutdown();
            kshook::http::shutdown();

            FreeLibraryAndExitThread(dll_instance, EXIT_FAILURE);
        },
        dll_instance, 0, nullptr);

    return TRUE;
}