#pragma once
#include <string>
#include <cstdint>

struct Config {
    // Archipelago connection
    std::string ap_host     = "archipelago.gg";
    uint16_t    ap_port     = 38281;
    std::string ap_slot     = "";      // player name
    std::string ap_password = "";
    std::string ap_game     = "Sound Voltex";

    // Game-side tuning
    // A location check is sent when a song is cleared (gauge filled).
    // music_id_offset: added to (music_id * 10 + difficulty) to get AP location id.
    int64_t location_base_id = 8000000;
    int64_t item_base_id     = 8000000;

    // Goal: how many clears are needed to win
    int     goal_clears      = 30;

    // Logging
    bool    debug_log        = false;
    std::string log_path     = "sdvx_ap.log";

    static Config load(const std::string& ini_path);
};
