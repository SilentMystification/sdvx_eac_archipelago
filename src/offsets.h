#pragma once
#include <cstdint>
#include <windows.h>

// ============================================================
// sv6c.exe offsets — version: SDVX Exceed Gear (Konasute)
// Image base: 0x140000000
// File date:  2026-06-05
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

// ---- Folder entry functions ------------------------------------------------
//
// FUN_1402fdae0 — universal "enter folder" function called for ALL folder types
// via the START-button (bVar6) code path in FUN_1402fc540.  Receives the outer
// handler object; the focused folder's ctx is at *(handler+0x1c8), and its
// type_id is at ctx+0x314.  Returning early (without calling the original)
// silently blocks folder entry.  Confirmed by Ghidra decompilation (2026-05-23).
//   ulonglong FUN_1402fdae0(longlong handler)
// NOTE: hooks.cpp locates this function at runtime via byte-pattern scan.
//       RVA_FOLDER_ENTER is kept as a fallback in case the pattern fails.
constexpr uintptr_t RVA_FOLDER_ENTER   = 0x302260;  // updated 2026-06-05 (was 0x2FDAE0)
constexpr ptrdiff_t FOLDER_HANDLER_CTX = 0x1C8;   // *(handler+0x1C8) → folder ctx ptr
constexpr ptrdiff_t FOLDER_CTX_TYPE_ID = 0x314;   // uint32_t type_id in ctx object
//
// FUN_1400f6ad0 — BLASTER GATE / Dimension-only entry gate.  Only called when
// ctx+0x20 == &LAB_1400f91f0 (Ghidra label for the BLASTER GATE state handler).
// LEVEL 1–20 folders have a different vtable entry at +0x20 and therefore NEVER
// reach this function.  Kept for documentation only.
//   uint64_t FUN_1400f6ad0(int64_t ctx, int32_t param2)
// constexpr uintptr_t RVA_FOLDER_ENTER_BLASTER = 0x0F6AD0;  // for reference

// ---- Folder vector (song-select screen) ------------------------------------
//
// Global triple { uint8_t* data; uint8_t* end; uint8_t* cap; } at this RVA.
// Lazily initialised the first time the player enters the song-select screen.
// Heap data pointed to by `data` is an array of 0xb8-byte folder structs.
//
// NOTE: Patching bytes in this struct does NOT prevent folder entry and does
// NOT change the folder's visual appearance at runtime.  The game enforces
// folder conditions through code in FUN_1400f6ad0, not through struct reads.
// The constants below are kept for documentation / future reference only.
//
// Discovered via Ghidra + Cheat Engine live-memory analysis (2026-05-23).
constexpr uintptr_t RVA_FOLDER_VEC     = 0xF10BE0;

// Per-entry layout within each 0xb8-byte folder struct (informational only)
constexpr size_t    FOLDER_STRIDE      = 0xb8;   // struct size
constexpr size_t    FOLDER_OFF_TYPE_ID = 0x68;   // uint32_t  — folder type id
// +0x90: uint8 — 0x01 for most folders, 0x00 for some network-gated ones
// +0x91: uint8 — 0x01 for unconditional folders, 0x00 for BLASTER GATE when
//                disabled; writing this byte has no observable UI/entry effect
// +0x92: uint8 — 0x01 across all 48 observed folder structs

// Type IDs for LEVEL 1–20 difficulty folders
// LEVEL N  →  FOLDER_TYPE_LEVEL1 + (N - 1),  N ∈ [1, 20]
constexpr uint32_t  FOLDER_TYPE_LEVEL1  = 0x14u; // LEVEL 1  (decimal 20)
constexpr uint32_t  FOLDER_TYPE_LEVEL20 = 0x27u; // LEVEL 20 (decimal 39)
