#pragma once

#include <nlohmann/json.hpp>
#include "../../../core/log.hpp"
#include "../../../common/sdvx/score_export.hpp"

namespace kshook::sv6c
{
    class score_export_request : public sdvx::score_export_request
    {
      public:
        explicit score_export_request(nlohmann::json data);

        std::vector<exported_score> get_scores() override;

      private:
        nlohmann::json _data = {};
        std::vector<exported_score> _scores = {};
    };
}