#include "log.hpp"
#include "http.hpp"
#include <ctpl_stl.h>
#include <buildinfo.hpp>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

const auto thread_count = 5;
const auto request_timeout = 10000L;

auto thread_pool = std::shared_ptr<ctpl::thread_pool>{};
auto global_headers = std::unordered_map<std::string, std::string>{
    {"Content-Type", "application/json"},
    {"User-Agent", KSHOOK_USER_AGENT},
};

bool kshook::http::init()
{
    auto result = curl_global_init(CURL_GLOBAL_DEFAULT);

    if (result == 0)
    {
        log::debug("Initialising request queue with {} thread(s)..", thread_count);
        thread_pool = std::make_shared<ctpl::thread_pool>(thread_count);
    }
    else
    {
        log::error("Failed to initialise cURL. ({})", static_cast<int>(result));
    }

    return (result == 0);
}

void kshook::http::shutdown()
{
    curl_global_cleanup();

    if (thread_pool == nullptr)
        return;

    thread_pool->clear_queue();
    thread_pool->stop(true);
}

void kshook::http::set_global_header(const std::string &header, const std::string &value)
{
    log::debug("Global header '{}' set to '{}'.", header, value);
    global_headers[header] = value;
}

bool post_request(const std::string &url, const kshook::http::headers_t &headers, const std::string &body, long &status)
{
    auto curl = curl_easy_init();
    auto header_list = curl_slist_append(nullptr, "Accept: application/json");

    auto _curl = std::shared_ptr<void>(nullptr, [curl](...) { curl_easy_cleanup(curl); });
    auto _header_list = std::shared_ptr<void>(nullptr, [header_list](...) { curl_slist_free_all(header_list); });

    if (curl == nullptr || header_list == nullptr)
    {
        kshook::log::error("Failed to initialise cURL request.");
        return false;
    }

    auto response_body = std::string{};
    auto request_headers = global_headers;

    request_headers.insert(headers.begin(), headers.end());

    for (const auto &header : request_headers)
        header_list = curl_slist_append(header_list, (header.first + ": " + header.second).c_str());

    if (kshook::log::is_debug())
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, request_timeout);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(
        curl, CURLOPT_WRITEFUNCTION, +[](void *ptr, std::size_t size, std::size_t nmemb, void *usr) {
            auto data = reinterpret_cast<const char *>(ptr);
            auto body = reinterpret_cast<std::string *>(usr);

            body->append(data, size * nmemb);

            return (size * nmemb);
        });

    auto result = curl_easy_perform(curl);

    if (result == CURLE_OPERATION_TIMEDOUT)
    {
        status = 408;
        kshook::log::error("Request to '{}' timed out.", url);
        return false;
    }
    else if (result != CURLE_OK)
    {
        kshook::log::error("cURL request failed. ({})", static_cast<int>(result));
        return false;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    if (status >= 200 && status <= 204)
    {
        kshook::log::info("Request to '{}' completed successfully.", url);
        return true;
    }

    auto error_reason = std::string("no reason specified");
    auto response_json = nlohmann::json::parse(response_body, nullptr, false);

    if (!response_json.is_discarded() && response_json.contains("error") && response_json["error"].is_string())
        error_reason = response_json["error"];

    kshook::log::error("Error received from '{}': {}", url, error_reason);

    return false;
}

void kshook::http::enqueue_request(const std::string &url, const headers_t &headers, const std::string &body)
{
    const auto max_attempts = 5;

    thread_pool->push([=](int thread_id) {
        log::info("Submitting request to '{}'..", url);

        for (auto attempt = 0; attempt < max_attempts; attempt++)
        {
            if (attempt > 0)
            {
                log::debug("Retrying request to '{}'.. (attempt #{})", url, attempt + 1);
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }

            auto status = 0L;

            if (post_request(url, headers, body, status))
                return;

            switch (status)
            {
            case 408: // Request Timeout
            case 503: // Service Unavailable
            case 504: // Gateway Timeout
                continue;
            default:
                return log::error("Request to '{}' failed. ({})", url, status);
            }
        }

        log::error("Request to '{}' failed after {} attempts.", url, max_attempts);
    });
}