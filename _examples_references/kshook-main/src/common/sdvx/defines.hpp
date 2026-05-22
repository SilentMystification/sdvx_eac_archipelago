#pragma once

namespace kshook::sdvx
{
    enum class difficulty_t
    {
        DIFFICULTY_NOVICE,
        DIFFICULTY_ADVANCED,
        DIFFICULTY_EXHAUST,
        DIFFICULTY_INFINITE,
        DIFFICULTY_MAXIMUM,
    };

    enum class rate_t
    {
        RATE_EFFECTIVE,
        RATE_PERMISSIVE,
        RATE_EXCESSIVE,
        RATE_BLASTIVE,
        RATE_MAXXIVE,
    };

    enum class grade_t
    {
        GRADE_D,
        GRADE_C,
        GRADE_B,
        GRADE_A,
        GRADE_A_PLUS,
        GRADE_AA,
        GRADE_AA_PLUS,
        GRADE_AAA,
        GRADE_AAA_PLUS,
        GRADE_S,
    };

    enum class clear_t
    {
        CLEAR_PLAYED,
        CLEAR_EFFECTIVE,
        CLEAR_EXCESSIVE,
        CLEAR_ULTIMATE_CHAIN,
        CLEAR_PERFECT,
        CLEAR_MAXXIVE,
    };
}