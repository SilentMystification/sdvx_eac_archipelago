#pragma once

#include <unordered_map>

namespace kshook::service
{
    struct entry
    {
        entry(std::string _url, std::string _token, std::vector<std::string> _games)
            : url(std::move(_url)), token(std::move(_token)), games(std::move(_games))
        {
            while (url.rbegin() != url.rend() && *url.rbegin() == '/')
                url.pop_back();

            if (!token.empty())
                headers["Authorization"] = "Bearer " + token;
        };

        std::string url = {};
        std::string token = {};
        std::vector<std::string> games = {};
        std::unordered_map<std::string, std::string> headers = {};
    };

    bool init(const std::filesystem::path &basedir);
    const std::vector<entry> &get_all();
}