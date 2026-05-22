#include "game.hpp"
#include "../../core/http.hpp"
#include "../../core/version.hpp"
#include "hooks/property_destroy.hpp"

bool kshook::sv6c::game::init()
{
    property_destroy_hook::install();

    return true;
}

bool kshook::sv6c::game::is_match(const nlohmann::json &config)
{
    try
    {
        auto info = version::get_info();

        auto filename = version::query_string(info, R"(\StringFileInfo\041104b0\OriginalFilename)");
        auto datecode = version::query_string(info, R"(\StringFileInfo\041104b0\ProductVersion)");

        if (filename != "sv6c.exe")
            return false;

        http::set_global_header("X-Software-Model", datecode);
    }
    catch (const std::exception &)
    {
        return false;
    }

    return true;
}

const char *kshook::sv6c::game::get_id()
{
    return "sv6c";
}

const char *kshook::sv6c::game::get_name()
{
    return "SOUND VOLTEX EXCEED GEAR";
}