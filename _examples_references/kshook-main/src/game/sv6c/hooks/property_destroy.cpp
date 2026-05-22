#include <MinHook.h>
#include <nlohmann/json.hpp>
#include "property_destroy.hpp"
#include "../../../avs2/avs2.hpp"
#include "../../../core/log.hpp"
#include "../../../core/http.hpp"
#include "../../../core/service.hpp"
#include "../requests/score_save.hpp"
#include "../requests/score_export.hpp"

void *(*original_destroy_fn)(void *) = nullptr;

inline bool is_valid_eacnet_property(void *property)
{
    if (property == nullptr)
        return false;

    if (avs2::property_search(property, nullptr, "eacnet") == nullptr)
        return false;

    return true;
}

bool property_to_json(void *property, nlohmann::json &body)
{
    avs2::property_set_flag(property, avs2::property_flags::PROP_XML, avs2::property_flags::PROP_BINARY);
    avs2::property_set_flag(property, avs2::property_flags::PROP_JSON, avs2::property_flags::PROP_XML);

    auto size = avs2::property_query_size(property);

    if (size < 0)
        return false;

    auto json = std::string(size, 0);

    if (avs2::property_mem_write(property, json.data(), json.size()) < 0)
        return original_destroy_fn(property);

    body = nlohmann::json::parse(json, nullptr, false);

    return !body.is_discarded();
}

void *replacement_destroy_fn(void *property)
{
    static auto last_request = std::string{};

    if (!is_valid_eacnet_property(property))
        return original_destroy_fn(property);

    auto body = nlohmann::json{};

    if (!property_to_json(property, body))
        return original_destroy_fn(property);

    auto is_request = body["eacnet"].contains("request");
    auto is_response = body["eacnet"].contains("response");

    if (!is_request && !is_response)
        return original_destroy_fn(property);

    auto root = body["eacnet"].at(is_request ? "request" : "response");

    if (is_request)
    {
        if (!root.contains("method") || !root.contains("module"))
            return original_destroy_fn(property);

        auto module = root.value("module", "");
        auto method = root.value("method", "");
        last_request = module + "." + method;

        kshook::log::debug("Updated last seen request to '{}'...", last_request);

        if (!root.contains("data") || !root["data"].is_object())
            return original_destroy_fn(property);

        auto data = root.at("data");

        if (!data.contains("track") || !data["track"].is_object())
            return original_destroy_fn(property);

        auto track = data.at("track");

        if (module != "game" || method != "sv6_save_m")
            return original_destroy_fn(property);

        auto request = kshook::sv6c::score_save_request(track).to_string();

        if (request.empty())
        {
            kshook::log::error("Discarded '{}.{}' request data.", module, method);
            return original_destroy_fn(property);
        }

        for (const auto &service : kshook::service::get_all())
        {
            if (std::find(service.games.begin(), service.games.end(), "sv6c") == service.games.end())
                continue;

            const auto url = fmt::format("{}/sv6c/{}/{}", service.url, "score", "save");

            kshook::http::enqueue_request(url, service.headers, request);
        }
    }

    if (is_response)
    {
        if (last_request != "game.sv6_load")
            return original_destroy_fn(property);

        if (!root.contains("game") || !root["game"].is_object())
            return original_destroy_fn(property);

        auto game = root.at("game");

        if (!game.contains("music") || !game["music"].is_object())
            return original_destroy_fn(property);

        auto music = game.at("music");

        if (!music.contains("info") || !music["info"].is_array())
            return original_destroy_fn(property);

        auto info = music.at("info");
        auto request = kshook::sv6c::score_export_request(info).to_string();

        if (request.empty())
        {
            kshook::log::error("Discarded 'game.sv6_load_m' request data.");
            return original_destroy_fn(property);
        }

        for (const auto &service : kshook::service::get_all())
        {
            if (std::find(service.games.begin(), service.games.end(), "sv6c") == service.games.end())
                continue;

            const auto url = fmt::format("{}/sv6c/{}/{}", service.url, "score", "export");

            kshook::http::enqueue_request(url, service.headers, request);
        }
    }

    return original_destroy_fn(property);
}

void kshook::sv6c::property_destroy_hook::install()
{
    avs2::resolve_v2173("avs2-core.dll");

    log::debug("Installing hook at {:#x} forwarding to {:#x}.", std::uintptr_t(avs2::property_destroy),
               std::uintptr_t(replacement_destroy_fn));

    MH_CreateHook(avs2::property_destroy, replacement_destroy_fn, reinterpret_cast<void **>(&original_destroy_fn));
    MH_EnableHook(avs2::property_destroy);
}
