#pragma once
#include <cstdint>
#include <windows.h>

// ── Named shared-memory IPC between version.dll and sdvx_ap_debug.exe ────────
//
// The DLL creates the mapping; the UI opens it.
// Protocol (lock-free, single-writer per field):
//
//   DLL writes:  magic, inputs, levels, seq_dll
//   UI  writes:  inputs, levels, seq_ui
//
// When the DLL detects seq_ui != last_seq_ui it reads inputs/levels from the
// shared block and applies them as overrides (same path as AP item callbacks).
// When the DLL receives AP items it writes the new masks and bumps seq_dll so
// the UI can reflect the current state without polling AP.

#define SDVX_AP_SHM_NAME    L"Local\\SDVX_AP_IPC_v1"
#define SDVX_AP_MUTEX_NAME  L"Local\\SDVX_AP_IPC_Mutex_v1"
constexpr uint32_t SDVX_AP_IPC_MAGIC = 0xAC8E0001u;
constexpr SIZE_T   SDVX_AP_IPC_SIZE  = 64u;  // rounded up; struct is ~20 bytes

// Bit layout for SdvxApSharedState::inputs  (matches InputSlot enum in dllmain.cpp):
//   bit 0 = BT-A   bit 1 = BT-B   bit 2 = BT-C   bit 3 = BT-D
//   bit 4 = FX-L   bit 5 = FX-R   bit 6 = START
//   bit 7 = Knob L  bit 8 = Knob R
//
// Bit layout for SdvxApSharedState::levels:
//   bit (N-1) = LEVEL N unlocked  (bits 0-19 for LEVEL 1-20)

#pragma pack(push, 1)
struct SdvxApSharedState {
    uint32_t          magic;      // SDVX_AP_IPC_MAGIC when valid
    uint16_t          inputs;     // input unlock bitmask (bits 0-8)
    uint16_t          _pad;
    uint32_t          levels;     // level folder unlock bitmask (bits 0-19)
    volatile uint32_t seq_dll;    // DLL increments each time it writes state
    volatile uint32_t seq_ui;     // UI increments each time it writes state
};
#pragma pack(pop)
