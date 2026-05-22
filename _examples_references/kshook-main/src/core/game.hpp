#pragma once

#include <nlohmann/json.hpp>

namespace kshook
{
    class game
    {
      public:
        virtual bool init() = 0;
        virtual bool is_match(const nlohmann::json &config) = 0;
        virtual const char *get_id() = 0;
        virtual const char *get_name() = 0;
    };
}