#include "score_export.hpp"

namespace kshook::sv6c
{
    score_export_request::score_export_request(nlohmann::json data) : _data(std::move(data))
    {
        for (auto const &container : _data)
        {
            try
            {
                if (!container.is_object() || !container.contains("param") || !container["param"].is_array())
                    continue;

                auto score = container["param"];

                auto const clear = static_cast<sdvx::clear_t>(score[4].get<std::uint32_t>() - 1);
                auto const grade = static_cast<sdvx::grade_t>(score[5].get<std::uint32_t>() - 1);

                if (clear < sdvx::clear_t::CLEAR_PLAYED || clear > sdvx::clear_t::CLEAR_MAXXIVE)
                    continue;

                if (grade < sdvx::grade_t::GRADE_D || grade > sdvx::grade_t::GRADE_S)
                    continue;

                _scores.push_back({
                    .music_id = score[0].get<decltype(exported_score::music_id)>(),
                    .difficulty = static_cast<sdvx::difficulty_t>(score[1].get<std::uint32_t>()),
                    .score = score[2].get<decltype(exported_score::score)>(),
                    .ex_score = score[3].get<decltype(exported_score::ex_score)>(),
                    .clear = clear,
                    .grade = grade,
                    .max_chain = score[6].get<decltype(exported_score::max_chain)>(),
                    .timestamp = score[16].get<decltype(exported_score::timestamp)>(),
                });
            }
            catch (const std::exception &error)
            {
                log::error("Failed to construct score save request. ({})", error.what());
            }
        }
    }

    std::vector<sdvx::score_export_request::exported_score> score_export_request::get_scores()
    {
        return _scores;
    }
}