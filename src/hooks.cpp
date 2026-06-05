// hooks.cpp — SDVX Archipelago
//
// Two independent hook groups:
//
// 1. avs2::property_destroy (XCgsqzn0000091 in avs2-core.dll)
//    Intercepts the "game.sv6_save_m" e-amusement network packet to extract
//    per-track result data (music_id, difficulty, score, clear_type, etc.).
//
// 2. sdvxio.dll — sdvx_io_get_input_gpio / sdvx_io_get_spinner_pos
//    Masks out inputs for buttons/knobs that have not yet been unlocked as
//    Archipelago items.  sdvxio.dll is the bemanitools SDVX IO abstraction
//    layer; by the time its exports are called, config.exe's physical-button
//    mapping has already been applied, so bit positions here are semantic:
//      gpio(0) bit 0=BT-A, 1=BT-B, 2=BT-C, 3=BT-D, 4=FX-L, 5=FX-R, 6=START
//    spinner(0) = left knob absolute position, spinner(1) = right knob.
//
// clear_type values in the sv6_save_m packet:
//   1=PLAYED  2=EFFECTIVE  3=EXCESSIVE  4=UC  5=PUC  6=MAXXIVE
//
// difficulty values (music_type field):
//   0=NOVICE  1=ADVANCED  2=EXHAUST  3=INF/GRAVITY/etc  4=MAXIMUM

#include "hooks.h"
#include "offsets.h"
#include "../include/MinHook/include/MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <json.hpp>

// ── Callbacks ─────────────────────────────────────────────────────────────────
static OnSessionResult g_on_session;
static OnTrackClear    g_on_clear;
static OnDiagLog       g_on_log;

static void diagf(const char* fmt, ...) {
    if (!g_on_log) return;
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    g_on_log(buf);
}

// ════════════════════════════════════════════════════════════════════════════
// GROUP 1 — AVS2 property_destroy hook (score tracking)
// ════════════════════════════════════════════════════════════════════════════

using avs2_fn_prop_search_t     = void*(void* prop, void* node, const char* path);
using avs2_fn_prop_destroy_t    = int32_t(void* prop);
using avs2_fn_prop_set_flag_t   = int32_t(void* prop, uint32_t flags_set, uint32_t flags_unset);
using avs2_fn_prop_query_size_t = int32_t(void* prop);
using avs2_fn_prop_mem_write_t  = int32_t(void* prop, void* data, uint32_t size);

static avs2_fn_prop_search_t*     avs2_prop_search     = nullptr;
static avs2_fn_prop_set_flag_t*   avs2_prop_set_flag   = nullptr;
static avs2_fn_prop_query_size_t* avs2_prop_query_size = nullptr;
static avs2_fn_prop_mem_write_t*  avs2_prop_mem_write  = nullptr;
static avs2_fn_prop_destroy_t*    orig_prop_destroy    = nullptr;

static constexpr uint32_t AVS_PROP_XML    = 0x000;
static constexpr uint32_t AVS_PROP_BINARY = 0x008;
static constexpr uint32_t AVS_PROP_JSON   = 0x800;
static constexpr uint32_t CLEAR_ANY       = 2;

static bool prop_to_json(void* prop, nlohmann::json& out) {
    avs2_prop_set_flag(prop, AVS_PROP_XML,  AVS_PROP_BINARY);
    avs2_prop_set_flag(prop, AVS_PROP_JSON, AVS_PROP_XML);

    int32_t sz = avs2_prop_query_size(prop);
    if (sz <= 0) return false;

    std::string buf(static_cast<size_t>(sz), '\0');
    if (avs2_prop_mem_write(prop, buf.data(), static_cast<uint32_t>(sz)) < 0)
        return false;

    out = nlohmann::json::parse(buf, nullptr, /*allow_exceptions=*/false);
    return !out.is_discarded();
}

static int32_t hk_property_destroy(void* prop) {
    if (!prop)
        return orig_prop_destroy(prop);

    // Stage 1: must have "eacnet" root node.
    void* eacnet_node = avs2_prop_search(prop, nullptr, "eacnet");
    if (!eacnet_node)
        return orig_prop_destroy(prop);

    // Stage 2: must have "request" child — skips internal eacnet IPC properties.
    void* req_node = avs2_prop_search(prop, eacnet_node, "request");
    if (!req_node)
        return orig_prop_destroy(prop);

    // Stage 3: expensive JSON conversion.
    nlohmann::json body;
    if (!prop_to_json(prop, body))
        return orig_prop_destroy(prop);

    if (!body.contains("eacnet") || !body["eacnet"].is_object())
        return orig_prop_destroy(prop);

    auto& eacnet = body["eacnet"];
    if (!eacnet.contains("request"))
        return orig_prop_destroy(prop);

    auto& req    = eacnet["request"];
    auto  module = req.value("module", std::string{});
    auto  method = req.value("method", std::string{});

    if (module != "game" || method != "sv6_save_m")
        return orig_prop_destroy(prop);

    diagf("[HOOK] Intercepted game.sv6_save_m");

    if (!req.contains("data") || !req["data"].contains("track"))
        return orig_prop_destroy(prop);

    auto& track = req["data"]["track"];

    try {
        uint32_t music_id   = track.value("music_id",   0u);
        uint32_t music_type = track.value("music_type", 0u);
        uint32_t score      = track.value("score",      0u);
        uint32_t exscore    = track.value("exscore",    0u);
        uint32_t clear_type = track.value("clear_type", 0u);
        uint32_t max_chain  = track.value("max_chain",  0u);
        uint32_t near_cnt   = track.value("near",       0u);
        uint32_t error_cnt  = track.value("error",      0u);
        uint32_t track_no   = track.value("track_no",   0u);

        diagf("[HOOK] track_no=%u music_id=%u diff=%u score=%u ex=%u "
              "clear_type=%u chain=%u near=%u err=%u",
              track_no, music_id, music_type, score, exscore,
              clear_type, max_chain, near_cnt, error_cnt);

        if (music_id != 0) {
            bool cleared = (clear_type >= CLEAR_ANY);
            if (cleared && g_on_clear)
                g_on_clear(music_id, music_type, clear_type);
            if (g_on_session) {
                TrackResult tr{};
                tr.music_id   = music_id;
                tr.difficulty = music_type;
                tr.score      = score;
                tr.ex_score   = exscore;
                tr.cleared    = cleared;
                g_on_session(&tr, 1);
            }
        } else {
            diagf("[HOOK] sv6_save_m: music_id=0, ignoring");
        }
    } catch (const std::exception& e) {
        diagf("[HOOK] Exception parsing sv6_save_m: %s", e.what());
    }

    return orig_prop_destroy(prop);
}

// ════════════════════════════════════════════════════════════════════════════
// GROUP 2 — sdvxio.dll input lock hooks
// ════════════════════════════════════════════════════════════════════════════

// Bits that are ALLOWED through (0 = locked, 1 = allowed).
// Atomics so dllmain can update from the AP receive thread safely.
static std::atomic<uint16_t> g_gpio0_allowed{0};    // bank 0: BT-A/B/C, START
static std::atomic<uint16_t> g_gpio1_allowed{0};    // bank 1: BT-D, FX-L, FX-R
static std::atomic<uint8_t>  g_spinner_allowed{0};  // spinners (bit0=L, bit1=R)

// Edge-detection state for button logging (only touched by the input thread).
static uint16_t g_gpio0_prev = 0;
static uint16_t g_gpio1_prev = 0;  // bank 1 — logged generically to locate BT-D/FX-L/FX-R

// Spinner freeze: when a knob is locked, we return the last value we sent so
// the game sees zero movement.  -1 = not yet seeded.
static int64_t g_spinner_frozen[2] = { -1, -1 };

// Per-knob log rate-limiting.  A line is only emitted when BOTH gates pass:
//   movement gate  — position moved >10 ticks from g_spinner_log_pos
//   time gate      — >=1 s elapsed since g_spinner_log_ms
// This stops logging when the knob is still AND caps fast spins to ~1 line/s.
static int64_t   g_spinner_log_pos[2] = { -1, -1 };
static ULONGLONG g_spinner_log_ms[2]  = {  0,  0 };

using sdvx_fn_gpio_t    = uint16_t(uint8_t bank);
using sdvx_fn_spinner_t = int64_t(uint8_t idx);

static sdvx_fn_gpio_t*    orig_sdvx_gpio    = nullptr;
static sdvx_fn_spinner_t* orig_sdvx_spinner = nullptr;

// Bank 0 bit-index to name (bits 0-3 confirmed, rest unused/unknown).
static const char* const k_gpio0_names[16] = {
    "BT-C",      "BT-B",      "BT-A",      "START",
    "gpio0[4]",  "gpio0[5]",  "gpio0[6]",  "gpio0[7]",
    "gpio0[8]",  "gpio0[9]",  "gpio0[10]", "gpio0[11]",
    "gpio0[12]", "gpio0[13]", "gpio0[14]", "gpio0[15]"
};

// Bank 1 bit-index to name (bits 3-5 confirmed).
static const char* const k_gpio1_names[16] = {
    "bank1[0]",  "bank1[1]",  "bank1[2]",  "FX-R",
    "FX-L",      "BT-D",      "bank1[6]",  "bank1[7]",
    "bank1[8]",  "bank1[9]",  "bank1[10]", "bank1[11]",
    "bank1[12]", "bank1[13]", "bank1[14]", "bank1[15]"
};

// ── Hook: sdvx_io_get_input_gpio ──────────────────────────────────────────────
static uint16_t hk_sdvx_io_get_input_gpio(uint8_t bank) {
    uint16_t raw = orig_sdvx_gpio(bank);

    // Bank 1: BT-D, FX-L, FX-R — filter and log like bank 0.
    if (bank == 1) {
        uint16_t allowed  = g_gpio1_allowed.load(std::memory_order_relaxed);
        uint16_t filtered = raw & allowed;

        uint16_t just_pressed  = raw  & ~g_gpio1_prev;
        uint16_t just_released = ~raw &  g_gpio1_prev;
        for (int i = 0; i < 16; ++i) {
            uint16_t bit = static_cast<uint16_t>(1u << i);
            if (just_pressed & bit) {
                if (allowed & bit)
                    diagf("[INPUT] %s PRESSED", k_gpio1_names[i]);
                else
                    diagf("[INPUT] %s suppressed (locked)", k_gpio1_names[i]);
            }
            if ((just_released & bit) && (allowed & bit))
                diagf("[INPUT] %s released", k_gpio1_names[i]);
        }
        g_gpio1_prev = raw;
        return filtered;
    }

    // All other banks (sys etc.): pass through silently.
    if (bank != 0)
        return raw;

    uint16_t allowed  = g_gpio0_allowed.load(std::memory_order_relaxed);
    uint16_t filtered = raw & allowed;

    // Log on transitions only.
    uint16_t just_pressed  = raw  & ~g_gpio0_prev;
    uint16_t just_released = ~raw &  g_gpio0_prev;

    for (int i = 0; i < 16; ++i) {
        uint16_t bit = static_cast<uint16_t>(1u << i);
        if (just_pressed & bit) {
            if (allowed & bit)
                diagf("[INPUT] %s PRESSED", k_gpio0_names[i]);
            else
                diagf("[INPUT] %s suppressed (locked)", k_gpio0_names[i]);
        }
        if ((just_released & bit) && (allowed & bit)) {
            // Only log release for buttons the game could actually see.
            diagf("[INPUT] %s released", k_gpio0_names[i]);
        }
    }

    g_gpio0_prev = raw;
    return filtered;
}

// ── Hook: sdvx_io_get_spinner_pos ─────────────────────────────────────────────
static int64_t hk_sdvx_io_get_spinner_pos(uint8_t idx) {
    int64_t raw = orig_sdvx_spinner(idx);
    if (idx > 1) return raw;

    const char* name    = (idx == 0) ? "LEFT" : "RIGHT";
    uint8_t     allowed = g_spinner_allowed.load(std::memory_order_relaxed);
    bool        locked  = !((allowed >> idx) & 1u);

    if (locked) {
        // Seed freeze + log-reference on first call while locked.
        if (g_spinner_frozen[idx] < 0) {
            g_spinner_frozen[idx]  = raw;
            g_spinner_log_pos[idx] = raw;
        }

        // Emit at most one line per second, and only when the knob has moved
        // >10 ticks from the last logged position (both gates must pass).
        int64_t delta = raw - g_spinner_log_pos[idx];
        if (delta < 0) delta = -delta;
        if (delta > 10) {
            ULONGLONG now = GetTickCount64();
            if (now - g_spinner_log_ms[idx] >= 1000) {
                diagf("[INPUT] Knob %s suppressed (locked) pos=%lld->frozen@%lld",
                      name, raw, g_spinner_frozen[idx]);
                g_spinner_log_pos[idx] = raw;
                g_spinner_log_ms[idx]  = now;
            }
        }

        return g_spinner_frozen[idx];
    }

    // Unlocked: pass through.  Keep freeze in sync so locking later doesn't jump.
    // Same dual gate for logging: >10 ticks moved AND >=1 s since last line.
    g_spinner_frozen[idx] = raw;

    int64_t delta = raw - g_spinner_log_pos[idx];
    if (delta < 0) delta = -delta;
    if (g_spinner_log_pos[idx] < 0 || delta > 10) {
        ULONGLONG now = GetTickCount64();
        if (g_spinner_log_pos[idx] < 0 || now - g_spinner_log_ms[idx] >= 1000) {
            diagf("[INPUT] Knob %s pos=%lld", name, raw);
            g_spinner_log_pos[idx] = raw;
            g_spinner_log_ms[idx]  = now;
        }
    }
    return raw;
}

// ── Install sdvxio hooks (optional) ──────────────────────────────────────────
static bool install_sdvxio_hooks() {
    HMODULE sdvxio = GetModuleHandleA("sdvxio.dll");
    if (!sdvxio) {
        diagf("[HOOK] sdvxio.dll not loaded — input lock hooks skipped "
              "(is dinputhook-sdvx.dll present in the game folder?)");
        return false;
    }

    auto* pfn_gpio = reinterpret_cast<sdvx_fn_gpio_t*>(
        GetProcAddress(sdvxio, "sdvx_io_get_input_gpio"));
    auto* pfn_spin = reinterpret_cast<sdvx_fn_spinner_t*>(
        GetProcAddress(sdvxio, "sdvx_io_get_spinner_pos"));

    if (!pfn_gpio || !pfn_spin) {
        diagf("[HOOK] sdvxio.dll exports not found — input lock hooks skipped");
        return false;
    }

    MH_STATUS mh;
    if ((mh = MH_CreateHook(reinterpret_cast<void*>(pfn_gpio),
                      reinterpret_cast<void*>(&hk_sdvx_io_get_input_gpio),
                      reinterpret_cast<void**>(&orig_sdvx_gpio))) != MH_OK) {
        diagf("[HOOK] MH_CreateHook(sdvx_io_get_input_gpio) failed: %s", MH_StatusToString(mh));
        return false;
    }

    if ((mh = MH_CreateHook(reinterpret_cast<void*>(pfn_spin),
                      reinterpret_cast<void*>(&hk_sdvx_io_get_spinner_pos),
                      reinterpret_cast<void**>(&orig_sdvx_spinner))) != MH_OK) {
        diagf("[HOOK] MH_CreateHook(sdvx_io_get_spinner_pos) failed: %s", MH_StatusToString(mh));
        return false;
    }

    diagf("[HOOK] sdvxio.dll input hooks installed — all inputs locked until AP unlocks them");
    return true;
}

// ── Input lock setter (called from dllmain) ───────────────────────────────────
void hooks_set_input_lock(uint16_t gpio0_allowed, uint16_t gpio1_allowed, uint8_t spinners_allowed) {
    g_gpio0_allowed.store(gpio0_allowed,      std::memory_order_relaxed);
    g_gpio1_allowed.store(gpio1_allowed,      std::memory_order_relaxed);
    g_spinner_allowed.store(spinners_allowed, std::memory_order_relaxed);
    diagf("[INPUT] Lock updated — gpio0=0x%02x gpio1=0x%02x spinners=0x%02x",
          gpio0_allowed, gpio1_allowed, spinners_allowed);
    for (int i = 0; i < 16; ++i)
        if (gpio0_allowed & (1u << i))
            diagf("[INPUT]   + %s unlocked", k_gpio0_names[i]);
    for (int i = 0; i < 16; ++i)
        if (gpio1_allowed & (1u << i))
            diagf("[INPUT]   + %s unlocked", k_gpio1_names[i]);
    if (spinners_allowed & SDVX_SPINNER_L) diagf("[INPUT]   + Knob LEFT unlocked");
    if (spinners_allowed & SDVX_SPINNER_R) diagf("[INPUT]   + Knob RIGHT unlocked");
}

// ════════════════════════════════════════════════════════════════════════════
// GROUP 3 — sv6c.exe folder entry hook (level lock)
// ════════════════════════════════════════════════════════════════════════════
//
// Hooks FUN_1402fdae0 (RVA_FOLDER_ENTER) — the universal "enter folder" function
// called for ALL folder types via the START-button (bVar6) path in FUN_1402fc540.
//
// Signature:  ulonglong FUN_1402fdae0(longlong handler)
//   handler+0x1C8  →  folder ctx pointer  (FOLDER_HANDLER_CTX)
//   ctx+0x314      →  uint32_t type_id     (FOLDER_CTX_TYPE_ID)
//
// IMPORTANT: FUN_1400f6ad0 (RVA 0x0F6AD0) is NOT the right hook target.
// It is only called for BLASTER GATE / Dimension folders (those whose
// ctx+0x20 == &LAB_1400f91f0); LEVEL 1–20 have a different vtable entry there
// and therefore never reach FUN_1400f6ad0.
// Confirmed by Ghidra decompilation of FUN_1402fc540 (2026-05-23).
//
// To block entry: return 0 early without calling the original.
// The return value is ignored by all callers, so blocking is silent.

// Bitmask of unlocked levels: bit (N-1) set = LEVEL N is enterable.
static std::atomic<uint32_t> g_levels_unlocked{0};

using FolderEnterFn = uint64_t(*)(int64_t handler);
static FolderEnterFn orig_folder_enter = nullptr;

static uint64_t hk_folder_enter(int64_t handler) {
    // handler+0x1C8 is a pointer to the folder ctx object.
    int64_t ctx = *reinterpret_cast<const int64_t*>(handler + FOLDER_HANDLER_CTX);
    if (!ctx) return orig_folder_enter(handler);   // safety: null ctx, pass through

    uint32_t tid = *reinterpret_cast<const uint32_t*>(ctx + FOLDER_CTX_TYPE_ID);
    diagf("[FOLDER] enter called: type_id=0x%02X", tid);   // DEBUG — remove after verified

    if (tid >= FOLDER_TYPE_LEVEL1 && tid <= FOLDER_TYPE_LEVEL20) {
        int      level = static_cast<int>(tid - FOLDER_TYPE_LEVEL1) + 1;
        uint32_t bit   = 1u << (level - 1);
        if (!(g_levels_unlocked.load(std::memory_order_relaxed) & bit)) {
            diagf("[FOLDER] Blocked entry to LEVEL %d (type_id=0x%02X, not yet unlocked)",
                  level, tid);
            return 0;   // skip original — folder is not entered
        }
    }
    return orig_folder_enter(handler);
}

void hooks_set_level_unlock(uint32_t levels_mask) {
    g_levels_unlocked.store(levels_mask, std::memory_order_relaxed);
    diagf("[FOLDER] Level unlock mask updated: 0x%05X", levels_mask);
    for (int i = 0; i < 20; ++i)
        if (levels_mask & (1u << i))
            diagf("[FOLDER]   + LEVEL %d unlocked", i + 1);
}

// ── Pattern scanner ───────────────────────────────────────────────────────────
// All sv6c.exe hooks must resolve their target via this scanner rather than a
// hardcoded RVA — recompilation shifts addresses, patterns survive it.
// mask: same length as pat; 'x' = exact match, '?' = wildcard byte.
static void* scan_pattern(const uint8_t* pat, const char* mask, size_t len) {
    HMODULE hMod = GetModuleHandleW(nullptr);
    auto*   dos  = reinterpret_cast<IMAGE_DOS_HEADER*>(hMod);
    auto*   nt   = reinterpret_cast<IMAGE_NT_HEADERS*>(
                       reinterpret_cast<uint8_t*>(hMod) + dos->e_lfanew);
    auto*   base = reinterpret_cast<uint8_t*>(hMod);
    size_t  size = nt->OptionalHeader.SizeOfImage;

    for (size_t i = 0; i + len <= size; ++i) {
        bool match = true;
        for (size_t j = 0; j < len && match; ++j)
            if (mask[j] != '?' && base[i + j] != pat[j])
                match = false;
        if (match) return base + i;
    }
    return nullptr;
}

// ════════════════════════════════════════════════════════════════════════════
// Install / remove all hooks
// ════════════════════════════════════════════════════════════════════════════

bool hooks_install(OnSessionResult cb_session, OnTrackClear cb_clear, OnDiagLog cb_log) {
    g_on_session = cb_session;
    g_on_clear   = cb_clear;
    g_on_log     = cb_log;

    // ── Resolve avs2-core.dll exports ────────────────────────────────────────
    HMODULE avs2 = GetModuleHandleA("avs2-core.dll");
    if (!avs2) {
        diagf("[HOOK] avs2-core.dll not loaded — cannot install hooks");
        return false;
    }

#define RESOLVE(var, export_name)                                              \
    var = reinterpret_cast<decltype(var)>(                                     \
              GetProcAddress(avs2, export_name));                               \
    if (!var) {                                                                \
        diagf("[HOOK] GetProcAddress failed for " export_name);                \
        return false;                                                          \
    }

    RESOLVE(avs2_prop_search,     "XCgsqzn00000a1")
    RESOLVE(orig_prop_destroy,    "XCgsqzn0000091")
    RESOLVE(avs2_prop_set_flag,   "XCgsqzn000009a")
    RESOLVE(avs2_prop_query_size, "XCgsqzn000009f")
    RESOLVE(avs2_prop_mem_write,  "XCgsqzn00000b8")
#undef RESOLVE

    diagf("[HOOK] avs2-core.dll exports resolved (prop_destroy @ %p)",
          reinterpret_cast<void*>(orig_prop_destroy));

    // ── Initialise MinHook ────────────────────────────────────────────────────
    MH_STATUS mh;
    if ((mh = MH_Initialize()) != MH_OK) {
        diagf("[HOOK] MH_Initialize failed: %s", MH_StatusToString(mh));
        return false;
    }

    // ── Hook avs2::property_destroy ───────────────────────────────────────────
    if ((mh = MH_CreateHook(
            reinterpret_cast<void*>(orig_prop_destroy),
            reinterpret_cast<void*>(&hk_property_destroy),
            reinterpret_cast<void**>(&orig_prop_destroy))) != MH_OK) {
        diagf("[HOOK] MH_CreateHook(property_destroy) failed: %s", MH_StatusToString(mh));
        return false;
    }

    // ── Hook sdvxio.dll input functions (optional, non-fatal) ─────────────────
    install_sdvxio_hooks();

    // ── Hook sv6c.exe folder entry function (pattern scan) ───────────────────
    // Pattern: prologue of FUN_1402fdae0 with struct field offsets wildcarded.
    // Derived from sv6c.exe 2026-05-20 via Ghidra; offsets at bytes 11-14,
    // 20-23, 26-29 are wildcarded so the scan survives recompilation.
    {
        static const uint8_t pat[] = {
            0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x40,  // push rbx; push rsi; push rdi; sub rsp,40h
            0x48, 0x8B, 0x81, 0x00, 0x00, 0x00, 0x00,          // mov rax,[rcx+????]
            0x48, 0x8B, 0xF1,                                   // mov rsi,rcx
            0x8B, 0xB8, 0x00, 0x00, 0x00, 0x00,                // mov edi,[rax+????]
            0x8B, 0x98, 0x00, 0x00, 0x00, 0x00                 // mov ebx,[rax+????]
        };
        static const char mask[] = "xxxxxxxxxxx????xxxxx????xx????";

        void* target = scan_pattern(pat, mask, sizeof(pat));
        if (target) {
            diagf("[HOOK] FolderEnter found by pattern scan @ %p", target);
        } else {
            diagf("[HOOK] FolderEnter pattern not found — falling back to RVA 0x%zX", RVA_FOLDER_ENTER);
            target = reinterpret_cast<void*>(RVA(RVA_FOLDER_ENTER));
        }

        if ((mh = MH_CreateHook(target,
                          reinterpret_cast<void*>(&hk_folder_enter),
                          reinterpret_cast<void**>(&orig_folder_enter))) != MH_OK) {
            diagf("[HOOK] MH_CreateHook(folder_enter) failed: %s (target=%p)",
                  MH_StatusToString(mh), target);
            return false;
        }
        diagf("[HOOK] Folder entry hook installed — LEVEL folders locked until AP items arrive");
    }

    // ── Enable all hooks ──────────────────────────────────────────────────────
    if ((mh = MH_EnableHook(MH_ALL_HOOKS)) != MH_OK) {
        diagf("[HOOK] MH_EnableHook failed: %s", MH_StatusToString(mh));
        return false;
    }

    diagf("[HOOK] avs2::property_destroy hook installed OK");
    return true;
}

void hooks_remove() {
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    orig_sdvx_gpio    = nullptr;
    orig_sdvx_spinner = nullptr;
}
