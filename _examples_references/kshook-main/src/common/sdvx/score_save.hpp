#pragma once

#include "defines.hpp"

namespace kshook::sdvx
{
    class score_save_request
    {
      public:
        std::string to_string();

        virtual difficulty_t get_difficulty() = 0;
        virtual rate_t get_rate() = 0;
        virtual grade_t get_grade() = 0;
        virtual clear_t get_clear() = 0;
        virtual std::uint16_t get_track_no() = 0;
        virtual std::uint32_t get_music_id() = 0;
        virtual std::uint32_t get_score() = 0;
        virtual std::uint32_t get_ex_score() = 0;
        virtual std::uint32_t get_max_chain() = 0;
        virtual std::uint32_t get_critical() = 0;
        virtual std::uint32_t get_near() = 0;
        virtual std::uint32_t get_error() = 0;
        virtual std::uint32_t get_gauge() = 0;
        virtual std::uint32_t get_retry_count() = 0;
    };
}