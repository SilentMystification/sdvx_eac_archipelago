#include "score_save.hpp"
#include "../../../core/exception.hpp"

namespace kshook::sv6c
{
    sdvx::difficulty_t score_save_request::get_difficulty()
    {
        const auto value = _data["music_type"].get<std::uint32_t>();

        switch (value)
        {
        case 0:
            return sdvx::difficulty_t::DIFFICULTY_NOVICE;
        case 1:
            return sdvx::difficulty_t::DIFFICULTY_ADVANCED;
        case 2:
            return sdvx::difficulty_t::DIFFICULTY_EXHAUST;
        case 3:
            return sdvx::difficulty_t::DIFFICULTY_INFINITE;
        case 4:
            return sdvx::difficulty_t::DIFFICULTY_MAXIMUM;
        }

        throw formatted_exception("unknown difficulty '{}' encountered", value);
    }

    sdvx::rate_t score_save_request::get_rate()
    {
        const auto value = _data["gauge_type"].get<std::uint8_t>();

        switch (value)
        {
        case 0:
            return sdvx::rate_t::RATE_EFFECTIVE;
        case 1:
            return sdvx::rate_t::RATE_PERMISSIVE;
        case 2:
            return sdvx::rate_t::RATE_EXCESSIVE;
        case 3:
            return sdvx::rate_t::RATE_BLASTIVE;
        case 19:
            return sdvx::rate_t::RATE_MAXXIVE;
        }

        throw formatted_exception("unexpected rate type '{}' encountered", value);
    }

    sdvx::grade_t score_save_request::get_grade()
    {
        const auto value = _data["score_grade"].get<std::uint32_t>();

        switch (value)
        {
        case 1:
            return sdvx::grade_t::GRADE_D;
        case 2:
            return sdvx::grade_t::GRADE_C;
        case 3:
            return sdvx::grade_t::GRADE_B;
        case 4:
            return sdvx::grade_t::GRADE_A;
        case 5:
            return sdvx::grade_t::GRADE_A_PLUS;
        case 6:
            return sdvx::grade_t::GRADE_AA;
        case 7:
            return sdvx::grade_t::GRADE_AA_PLUS;
        case 8:
            return sdvx::grade_t::GRADE_AAA;
        case 9:
            return sdvx::grade_t::GRADE_AAA_PLUS;
        case 10:
            return sdvx::grade_t::GRADE_S;
        default:
            throw formatted_exception("unexpected grade '{}' encountered", value);
        }
    }

    sdvx::clear_t score_save_request::get_clear()
    {
        const auto value = _data["clear_type"].get<std::uint32_t>();

        switch (value)
        {
        case 1:
            return sdvx::clear_t::CLEAR_PLAYED;
        case 2:
            return sdvx::clear_t::CLEAR_EFFECTIVE;
        case 3:
            return sdvx::clear_t::CLEAR_EXCESSIVE;
        case 4:
            return sdvx::clear_t::CLEAR_ULTIMATE_CHAIN;
        case 5:
            return sdvx::clear_t::CLEAR_PERFECT;
        case 6:
            return sdvx::clear_t::CLEAR_MAXXIVE;
        }

        throw formatted_exception("unexpected clear type '{}' encountered", value);
    }

    std::uint16_t score_save_request::get_track_no()
    {
        return _data["track_no"];
    }

    std::uint32_t score_save_request::get_music_id()
    {
        return _data["music_id"];
    }

    std::uint32_t score_save_request::get_score()
    {
        return _data["score"];
    }

    std::uint32_t score_save_request::get_ex_score()
    {
        return _data["exscore"];
    }

    std::uint32_t score_save_request::get_max_chain()
    {
        return _data["max_chain"];
    }

    std::uint32_t score_save_request::get_critical()
    {
        return _data["critical"];
    }

    std::uint32_t score_save_request::get_near()
    {
        return _data["near"];
    }

    std::uint32_t score_save_request::get_error()
    {
        return _data["error"];
    }

    std::uint32_t score_save_request::get_gauge()
    {
        return _data["effective_rate"];
    }

    std::uint32_t score_save_request::get_retry_count()
    {
        return _data["retry_cnt"];
    }
}