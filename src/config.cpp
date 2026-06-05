#include "config.h"
#include <windows.h>
#include <string>
#include <cstdlib>

static std::string GetIniString(const char* section, const char* key,
                                const char* def, const char* path) {
    char buf[512] = {};
    GetPrivateProfileStringA(section, key, def, buf, sizeof(buf), path);
    return buf;
}

static int GetIniInt(const char* section, const char* key,
                     int def, const char* path) {
    return static_cast<int>(
        GetPrivateProfileIntA(section, key, def, path));
}

Config Config::load(const std::string& ini_path) {
    Config c;
    const char* p = ini_path.c_str();

    c.ap_host     = GetIniString("Archipelago", "host",     "archipelago.gg", p);
    c.ap_port     = static_cast<uint16_t>(GetIniInt("Archipelago", "port", 38281, p));
    c.ap_slot     = GetIniString("Archipelago", "slot",     "", p);
    c.ap_password = GetIniString("Archipelago", "password", "", p);
    c.ap_game     = GetIniString("Archipelago", "game",     "Sound Voltex", p);

    c.location_base_id = static_cast<int64_t>(GetIniInt("Game", "location_base_id", 8100000, p));
    c.item_base_id     = static_cast<int64_t>(GetIniInt("Game", "item_base_id",     8000000, p));
    c.goal_clears      = GetIniInt("Game", "goal_clears",      30, p);
    c.goal_mode        = GetIniInt("Game", "goal_mode",         0, p);
    c.goal_song_count  = GetIniInt("Game", "goal_song_count",   5, p);

    c.debug_log     = GetIniInt("Debug", "enabled", 0, p) != 0;
    c.log_path      = GetIniString("Debug", "log_path", "sdvx_ap.log", p);

    return c;
}
