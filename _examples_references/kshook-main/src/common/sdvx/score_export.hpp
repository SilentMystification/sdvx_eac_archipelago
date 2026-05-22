#pragma once

#include "defines.hpp"

namespace kshook::sdvx
{
    class score_export_request
    {
      public:
        struct exported_score
        {
            std::uint32_t music_id;
            difficulty_t difficulty;
            std::uint32_t score;
            std::uint32_t ex_score;
            clear_t clear;
            grade_t grade;
            std::uint32_t max_chain;
            std::uint64_t timestamp;
        };

        std::string to_string();
        virtual std::vector<exported_score> get_scores() = 0;
    };
}