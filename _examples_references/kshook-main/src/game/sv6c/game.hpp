#pragma once

#include "../../core/game.hpp"

namespace kshook::sv6c
{
    class game : public kshook::game
    {
      public:
        bool init() override;
        bool is_match(const nlohmann::json &config) override;
        const char *get_id() override;
        const char *get_name() override;
    };
}