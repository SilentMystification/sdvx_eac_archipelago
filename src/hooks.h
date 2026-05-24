#pragma once
#include <functional>
#include <cstdint>
#include <string>

// ── Track result ──────────────────────────────────────────────────────────────
struct TrackResult {
    uint32_t music_id   = 0;
    uint32_t difficulty = 0;   // 0=NOV 1=ADV 2=EXH 3=INF/MXM
    uint32_t score      = 0;
    uint32_t ex_score   = 0;
    bool     cleared    = false;
    // clear_type from the sv6_save_m network packet:
    //   1=PLAYED  2=EFFECTIVE  3=EXCESSIVE  4=UC  5=PUC  6=MAXXIVE
    // 0 means not set (e.g. struct was zero-initialised).
};

// ── SDVX input bitmasks ───────────────────────────────────────────────────────
// Bank 0: BT-A, BT-B, BT-C, START (confirmed by live testing)
static constexpr uint16_t SDVX_GPIO0_BT_A  = 0x04;   // bit 2
static constexpr uint16_t SDVX_GPIO0_BT_B  = 0x02;   // bit 1
static constexpr uint16_t SDVX_GPIO0_BT_C  = 0x01;   // bit 0
static constexpr uint16_t SDVX_GPIO0_START = 0x08;   // bit 3
static constexpr uint16_t SDVX_GPIO0_ALL_BUTTONS =
    SDVX_GPIO0_BT_A | SDVX_GPIO0_BT_B | SDVX_GPIO0_BT_C | SDVX_GPIO0_START;

// Bank 1: BT-D, FX-L, FX-R (confirmed by live testing)
static constexpr uint16_t SDVX_GPIO1_BT_D  = 0x20;   // bit 5
static constexpr uint16_t SDVX_GPIO1_FX_L  = 0x10;   // bit 4
static constexpr uint16_t SDVX_GPIO1_FX_R  = 0x08;   // bit 3
static constexpr uint16_t SDVX_GPIO1_ALL_BUTTONS =
    SDVX_GPIO1_BT_D | SDVX_GPIO1_FX_L | SDVX_GPIO1_FX_R;

// sdvx_io_get_spinner_pos(idx) — bit 0 = left knob, bit 1 = right knob.
static constexpr uint8_t SDVX_SPINNER_L   = 0x01;
static constexpr uint8_t SDVX_SPINNER_R   = 0x02;
static constexpr uint8_t SDVX_SPINNER_ALL = SDVX_SPINNER_L | SDVX_SPINNER_R;

// ── Callbacks ─────────────────────────────────────────────────────────────────
// Callback fired once per track when the sv6_save_m packet is intercepted.
// count is always 1 from the new avs2 hook path.
using OnSessionResult = std::function<void(const TrackResult tracks[], int count)>;

// Callback fired when a track clear is detected (clear_type >= EFFECTIVE).
// music_id / difficulty come directly from the network packet.
using OnTrackClear = std::function<void(uint32_t music_id, uint32_t difficulty)>;

// Diagnostic log callback — routes hooks.cpp output to the main log file.
using OnDiagLog = std::function<void(const std::string& msg)>;

// ── Hook install / remove ─────────────────────────────────────────────────────
bool hooks_install(OnSessionResult cb_session, OnTrackClear cb_clear, OnDiagLog cb_log = nullptr);
void hooks_remove();

// ── Input lock control ────────────────────────────────────────────────────────
// Sets which SDVX inputs are allowed through the sdvxio hook (bit=1 = ALLOWED).
// Call with (0, 0, 0) to lock everything.  OR in SDVX_GPIO0_* / SDVX_GPIO1_* /
// SDVX_SPINNER_* constants for each unlocked item.  Thread-safe; may be called
// at any time after hooks_install returns true.
void hooks_set_input_lock(uint16_t gpio0_allowed, uint16_t gpio1_allowed, uint8_t spinners_allowed);

// ── Level folder lock control ─────────────────────────────────────────────────
// Bitmask: bit (N-1) = 1 means LEVEL N folder is unlocked (player can enter).
// Starts at 0 (all locked).  Thread-safe; may be called at any time after
// hooks_install returns true.
void hooks_set_level_unlock(uint32_t levels_mask);
