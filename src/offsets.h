#pragma once
#include <cstdint>
#include <windows.h>

// ============================================================
// sv6c.exe offsets — version: SDVX Exceed Gear (Konasute)
// Image base: 0x140000000
// File date:  2026-05-20 (Ghidra creation date)
// ============================================================

// RVA helper: adds offset to the runtime base of sv6c.exe
inline uintptr_t RVA(uintptr_t offset) {
    return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + offset;
}

// ---- Function offsets (relative to image base 0x140000000) ----------------

// Called at session end; builds per-track result XML/event for e-amusement.
// void FUN_1403ff640(undefined8 param_1, undefined8 param_2)
constexpr uintptr_t RVA_RESULT_UPLOAD  = 0x3FF640;

// Returns "clear", "normal", or "" for the current track's gauge result.
// undefined8* FUN_14029e620(undefined8* out, longlong movieclip_ctx)
constexpr uintptr_t RVA_CLEAR_STATE    = 0x29E620;

// BM2D state-machine dispatcher  (called with state name + context string)
// void FUN_1404031f0(const char* state_name, const char* ctx)
constexpr uintptr_t RVA_STATE_DISPATCH = 0x4031F0;

// ---- Data offsets ----------------------------------------------------------

// Pointer to the Konasute session result struct (read in FUN_1403ff640)
constexpr uintptr_t RVA_RESULT_STATE   = 0xEF3E60;

// BM2D property getter function pointer
// int (*fn)(int mc_handle, int type, const char* name, void* out_val)
constexpr uintptr_t RVA_PROP_GETTER    = 0x611958;

// ---- Per-session result struct layout (pointed to by RVA_RESULT_STATE) -----
//
//  +0x08        char  :  abort flag
//  +0x34        u32   :  megamix_id
//  +0x38        u32   :  play_id
//
//  Track[n] starts at 0x3c + n*0x30  (n = 0,1,2 for up to 3 tracks)
//    +0x00  u32  music_id
//    +0x04  u32  music_type  (difficulty: 0=NOV 1=ADV 2=EXH 3=MXM/INF)
//    +0x08  u32  score
//    +0x0c  u32  ex_score
//    (remaining 12 dwords: grade, near/error counts, etc.)
//
//  Byte region at +0xcc: per-track clear/gauge flags (1 byte each, up to 12)

constexpr uintptr_t RESULT_TRACK0_BASE = 0x3C;
constexpr uintptr_t RESULT_TRACK_STRIDE = 0x30;
constexpr uint32_t  RESULT_MAX_TRACKS   = 3;

// Within each 0x30-byte track block
constexpr uintptr_t TRACK_OFF_MUSIC_ID   = 0x00;
constexpr uintptr_t TRACK_OFF_MUSIC_TYPE = 0x04;  // difficulty
constexpr uintptr_t TRACK_OFF_SCORE      = 0x08;
constexpr uintptr_t TRACK_OFF_EX_SCORE   = 0x0C;

// Byte region: clear flag per track (0 = fail, 1 = cleared)
constexpr uintptr_t RESULT_CLEAR_FLAGS   = 0xCC;

// ---- BM2D property op codes ------------------------------------------------
constexpr int BM2D_GET_INT_BY_NAME = 0x1012;
constexpr int BM2D_GET_VEC2        = 0x1008;
