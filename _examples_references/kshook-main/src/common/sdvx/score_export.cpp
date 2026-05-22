#include <nlohmann/json.hpp>
#include <magic_enum/magic_enum.hpp>
#include "score_export.hpp"
#include "../../core/log.hpp"

std::string kshook::sdvx::score_export_request::to_string()
{
    auto result = nlohmann::json{};

    try
    {
        result["scores"] = {};

        for (auto const &scores : get_scores())
        {
            auto score = nlohmann::json{};

            score["music_id"] = scores.music_id;
            score["difficulty"] = magic_enum::enum_name(scores.difficulty);
            score["score"] = scores.score;
            score["ex_score"] = scores.ex_score;
            score["clear"] = magic_enum::enum_name(scores.clear);
            score["grade"] = magic_enum::enum_name(scores.grade);
            score["max_chain"] = scores.max_chain;
            score["timestamp"] = scores.timestamp;

            result["scores"].push_back(score);
        }

        return result.dump();
    }
    catch (const std::exception &error)
    {
        log::error("Failed to construct score export request. ({})", error.what());
    }

    return "";
}