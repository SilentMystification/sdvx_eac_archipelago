#pragma once

#include <nlohmann/json.hpp>
#include "../../../common/sdvx/score_save.hpp"

namespace kshook::sv6c
{
    class score_save_request : public sdvx::score_save_request
    {
      public:
        explicit score_save_request(nlohmann::json data) : _data(std::move(data)) {};

        sdvx::difficulty_t get_difficulty() override;
        sdvx::rate_t get_rate() override;
        sdvx::grade_t get_grade() override;
        sdvx::clear_t get_clear() override;
        std::uint16_t get_track_no() override;
        std::uint32_t get_music_id() override;
        std::uint32_t get_score() override;
        std::uint32_t get_ex_score() override;
        std::uint32_t get_max_chain() override;
        std::uint32_t get_critical() override;
        std::uint32_t get_near() override;
        std::uint32_t get_error() override;
        std::uint32_t get_gauge() override;
        std::uint32_t get_retry_count() override;

      private:
        nlohmann::json _data = {};
    };
}