#include <nlohmann/json.hpp>
#include <magic_enum/magic_enum.hpp>
#include "score_save.hpp"
#include "../../core/log.hpp"

std::string kshook::sdvx::score_save_request::to_string()
{
    auto result = nlohmann::json{};

    try
    {
        result["difficulty"] = magic_enum::enum_name(get_difficulty());
        result["rate"] = magic_enum::enum_name(get_rate());
        result["grade"] = magic_enum::enum_name(get_grade());
        result["clear"] = magic_enum::enum_name(get_clear());
        result["track_no"] = get_track_no();
        result["music_id"] = get_music_id();
        result["score"] = get_score();
        result["ex_score"] = get_ex_score();
        result["max_chain"] = get_max_chain();
        result["critical"] = get_critical();
        result["near"] = get_near();
        result["error"] = get_error();
        result["gauge"] = get_gauge();
        result["retry_count"] = get_retry_count();

        return result.dump();
    }
    catch (const std::exception &error)
    {
        log::error("Failed to construct score save request. ({})", error.what());
    }

    return "";
}