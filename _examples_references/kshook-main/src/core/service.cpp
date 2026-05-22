#include "log.hpp"
#include "service.hpp"
#include <fstream>
#include <nlohmann/json.hpp>

auto services = std::vector<kshook::service::entry>{};

std::vector<std::filesystem::path> find_services(const std::filesystem::path &basedir)
{
    auto result = std::vector<std::filesystem::path>{};

    for (const auto &entry : std::filesystem::directory_iterator(basedir))
    {
        if (entry.is_directory())
            continue;

        if (!entry.path().string().ends_with(".kshook.json"))
            continue;

        result.push_back(entry.path());
    }

    return result;
}

bool parse_service(const std::filesystem::path &path)
{
    auto file = std::ifstream(path);
    auto contents = nlohmann::json::parse(file, nullptr, false);

    if (contents.is_discarded() || !contents.is_object())
    {
        kshook::log::error("Failed to parse service file '{}'.", path.filename().string());
        return false;
    }

    if (!contents["url"].is_string() || contents["url"].empty())
    {
        kshook::log::error("No URL found in service file '{}'.", path.filename().string());
        return false;
    }

    if (!contents["games"].is_array() || contents["games"].empty())
    {
        kshook::log::error("No game list found in service file '{}'.", path.filename().string());
        return false;
    }

    for (const auto &game : contents["games"])
    {
        if (!game.is_string())
        {
            kshook::log::error("Invalid game value in service file '{}'.", path.filename().string());
            return false;
        }
    }

    services.emplace_back(contents["url"], contents["token"], contents["games"]);

    if (!contents["token"].empty())
        kshook::log::info("Registered authenticated service from file '{}'.", path.filename().string());
    else
        kshook::log::info("Registered service from file '{}'.", path.filename().string());

    return true;
}

bool kshook::service::init(const std::filesystem::path &basedir)
{
    bool has_service = false;

    for (const auto &path : find_services(basedir))
        has_service = parse_service(path) || has_service;

    return has_service;
}

const std::vector<kshook::service::entry> &kshook::service::get_all()
{
    return services;
}