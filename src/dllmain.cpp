#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <set>
#include <cstdint>

#include "config.h"
#include "hooks.h"
#include "ap_client.h"
#include "offsets.h"
#include "ipc.h"
// ============================================================
// Forward declarations
// ============================================================
static void log(const std::string& msg);  // defined below in Logging section
static void apply_input_lock();           // defined below in AP event callbacks section

// ============================================================
// Globals
// ============================================================
static Config    g_cfg;
static APClient  g_ap;

static std::mutex             g_cleared_mutex;
static std::set<APLocationID> g_cleared_locations; // already sent this session
static int                    g_total_clears = 0;
static int                    g_goal_songs   = 0;
static bool                   g_goal_sent    = false;

static std::ofstream g_log;
static std::mutex    g_log_mutex;

// ── AP item ID offsets (relative to item_base_id) ────────────────────────────
constexpr int INPUT_ITEM_BASE   = 0;   constexpr int INPUT_ITEM_COUNT   = 9;
constexpr int CLEAR_ITEM_BASE   = 20;  constexpr int CLEAR_ITEM_COUNT   = 5;
constexpr int GOAL_SONG_REL     = 50;
constexpr int LEVEL_ITEM_BASE   = 101; constexpr int LEVEL_ITEM_COUNT   = 20;

// ── Input unlock state ────────────────────────────────────────────────────────
// Input items: item_base_id + 0..8 (BT-A…START, Knob L/R).
// Clear type items: item_base_id + 20..24 (Clear, Hard Clear, Maxxive Clear, UC, PUC).
// Goal Song: item_base_id + 50.
// Level folder items: item_base_id + 101..120 (LEVEL 1–20).
// Location IDs: location_base_id + music_id*10 + difficulty — no overlap with items.

enum InputSlot : int {
    INPUT_BT_A   = 0,
    INPUT_BT_B   = 1,
    INPUT_BT_C   = 2,
    INPUT_BT_D   = 3,
    INPUT_FX_L   = 4,
    INPUT_FX_R   = 5,
    INPUT_START  = 6,
    INPUT_KNOB_L = 7,
    INPUT_KNOB_R = 8,
    INPUT_COUNT  = 9,
};

// Accumulated unlock masks — only ever ORed into, never cleared this session.
static std::mutex g_input_mutex;
static uint16_t   g_gpio0_unlocked   = 0;
static uint16_t   g_gpio1_unlocked   = 0;
static uint8_t    g_spinner_unlocked = 0;

// g_total_clears / g_goal_sent declared in the Globals section above.

// ── Level-folder unlock state ─────────────────────────────────────────────────
// Accumulated bitmask of unlocked levels — only ever ORed into, never cleared.
// Forwarded to hooks.cpp via hooks_set_level_unlock() where the actual entry
// gate lives (hook on FUN_1402fdae0 / RVA_FOLDER_ENTER).
static uint32_t g_levels_unlocked_local = 0u;

// ── IPC (debug UI) state ──────────────────────────────────────────────────────
static HANDLE   g_shm_handle     = nullptr;
static SdvxApSharedState* g_shm  = nullptr;
static HANDLE   g_ipc_stop_evt   = nullptr;
static HANDLE   g_ipc_thread     = nullptr;

// Compute 9-bit inputs bitmask from the current GPIO/spinner masks.
// Must be called under g_input_mutex.
static uint16_t compute_inputs_mask() {
    uint16_t m = 0;
    if (g_gpio0_unlocked   & SDVX_GPIO0_BT_A)  m |= (1u << INPUT_BT_A);
    if (g_gpio0_unlocked   & SDVX_GPIO0_BT_B)  m |= (1u << INPUT_BT_B);
    if (g_gpio0_unlocked   & SDVX_GPIO0_BT_C)  m |= (1u << INPUT_BT_C);
    if (g_gpio1_unlocked   & SDVX_GPIO1_BT_D)  m |= (1u << INPUT_BT_D);
    if (g_gpio1_unlocked   & SDVX_GPIO1_FX_L)  m |= (1u << INPUT_FX_L);
    if (g_gpio1_unlocked   & SDVX_GPIO1_FX_R)  m |= (1u << INPUT_FX_R);
    if (g_gpio0_unlocked   & SDVX_GPIO0_START)  m |= (1u << INPUT_START);
    if (g_spinner_unlocked & SDVX_SPINNER_L)    m |= (1u << INPUT_KNOB_L);
    if (g_spinner_unlocked & SDVX_SPINNER_R)    m |= (1u << INPUT_KNOB_R);
    return m;
}

// Apply a 9-bit inputs bitmask to the GPIO/spinner masks.
// Replaces (does not OR) the current masks.  Must be called under g_input_mutex.
static void apply_inputs_mask(uint16_t m) {
    g_gpio0_unlocked   = 0;
    g_gpio1_unlocked   = 0;
    g_spinner_unlocked = 0;
    if (m & (1u << INPUT_BT_A))   g_gpio0_unlocked   |= SDVX_GPIO0_BT_A;
    if (m & (1u << INPUT_BT_B))   g_gpio0_unlocked   |= SDVX_GPIO0_BT_B;
    if (m & (1u << INPUT_BT_C))   g_gpio0_unlocked   |= SDVX_GPIO0_BT_C;
    if (m & (1u << INPUT_BT_D))   g_gpio1_unlocked   |= SDVX_GPIO1_BT_D;
    if (m & (1u << INPUT_FX_L))   g_gpio1_unlocked   |= SDVX_GPIO1_FX_L;
    if (m & (1u << INPUT_FX_R))   g_gpio1_unlocked   |= SDVX_GPIO1_FX_R;
    if (m & (1u << INPUT_START))  g_gpio0_unlocked   |= SDVX_GPIO0_START;
    if (m & (1u << INPUT_KNOB_L)) g_spinner_unlocked |= SDVX_SPINNER_L;
    if (m & (1u << INPUT_KNOB_R)) g_spinner_unlocked |= SDVX_SPINNER_R;
}

// Write current DLL state to shared memory and bump seq_dll so the UI updates.
// Safe to call with or without the IPC mapping open.
// Must be called under g_input_mutex.
static void ipc_write_dll_state() {
    if (!g_shm) return;
    g_shm->inputs = compute_inputs_mask();
    g_shm->levels = g_levels_unlocked_local;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shm->seq_dll));
}

// ── IPC poll thread ───────────────────────────────────────────────────────────
// Runs at 100 ms intervals.  When the UI increments seq_ui, reads the new
// inputs/levels masks and applies them as an override (replaces AP-derived state).
static DWORD WINAPI ipc_poll_thread(LPVOID) {
    uint32_t last_seq_ui = 0;
    while (WaitForSingleObject(g_ipc_stop_evt, 100) == WAIT_TIMEOUT) {
        if (!g_shm) continue;
        uint32_t seq = g_shm->seq_ui;
        if (seq == last_seq_ui) continue;
        last_seq_ui = seq;

        uint16_t new_inputs = g_shm->inputs;
        uint32_t new_levels = g_shm->levels;

        {
            std::lock_guard<std::mutex> lk(g_input_mutex);
            apply_inputs_mask(new_inputs);
            g_levels_unlocked_local = new_levels;
            apply_input_lock();
            // (do NOT call ipc_write_dll_state here — we just read from the UI)
        }
        hooks_set_level_unlock(new_levels);
        log("[IPC] UI override applied: inputs=0x" +
            [&]{ char b[8]; snprintf(b,sizeof(b),"%03X",new_inputs); return std::string(b); }() +
            " levels=0x" +
            [&]{ char b[10]; snprintf(b,sizeof(b),"%05X",new_levels); return std::string(b); }());
    }
    return 0;
}

// ============================================================
// Logging
// ============================================================
static void log(const std::string& msg) {
    if (!g_cfg.debug_log) return;
    std::lock_guard<std::mutex> lk(g_log_mutex);
    if (g_log.is_open()) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char ts[32];
        snprintf(ts, sizeof(ts), "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
        g_log << ts << msg << "\n";
        g_log.flush();
    }
}

// ============================================================
// Location / item ID helpers
// ============================================================
// Location: base + music_id*10 + difficulty
//   difficulty: 0=NOV 1=ADV 2=EXH 3=MXM/INF/higher
static APLocationID make_location_id(uint32_t music_id, uint32_t difficulty) {
    return g_cfg.location_base_id + (APLocationID)music_id * 10 + (APLocationID)difficulty;
}

// ============================================================
// Hook callbacks
// ============================================================

// Called by hooks.cpp when a cleared sv6_save_m packet is intercepted.
static void on_track_clear(uint32_t music_id, uint32_t difficulty, uint32_t clear_type) {
    if (music_id == 0) {
        log("[HOOK] track clear fired but music_id=0");
        return;
    }

    static const struct { uint32_t ct; const char* name; } k_clear_map[] = {
        { 2, "Clear" }, { 3, "Hard Clear" }, { 6, "Maxxive Clear" },
        { 4, "UC" }, { 5, "PUC" },
    };
    const char* clear_name = "Unknown";
    for (auto& e : k_clear_map)
        if (e.ct == clear_type) { clear_name = e.name; break; }

    APLocationID loc_id = make_location_id(music_id, difficulty);

    {
        std::lock_guard<std::mutex> lk(g_cleared_mutex);
        if (g_cleared_locations.count(loc_id)) return;
        g_cleared_locations.insert(loc_id);
    }

    log("[AP] Track cleared: music_id=" + std::to_string(music_id) +
        " diff=" + std::to_string(difficulty) +
        " clear=" + clear_name +
        " location_id=" + std::to_string(loc_id));

    if (g_ap.is_connected()) {
        g_ap.send_location_checks({loc_id});
    } else {
        log("[AP] Not connected — location check queued for reconnect");
    }
}

// Called by hooks.cpp after the full sv6_save_m packet is parsed (diagnostic only).
static void on_session_result(const TrackResult tracks[], int count) {
    for (int i = 0; i < count; i++) {
        log("[HOOK] sv6_save_m: music_id=" + std::to_string(tracks[i].music_id) +
            " diff=" + std::to_string(tracks[i].difficulty) +
            " score=" + std::to_string(tracks[i].score) +
            " ex=" + std::to_string(tracks[i].ex_score) +
            " cleared=" + (tracks[i].cleared ? "YES" : "NO"));
    }
}

// ============================================================
// AP event callbacks
// ============================================================

// Helper — recompute the input lock from the accumulated unlock masks, push it
// to hooks.cpp, and mirror the new state to shared memory so the debug UI
// reflects it.  Must be called under g_input_mutex.
static void apply_input_lock() {
    hooks_set_input_lock(g_gpio0_unlocked, g_gpio1_unlocked, g_spinner_unlocked);
    ipc_write_dll_state();
}

static void on_ap_items(const std::vector<APItem>& items) {
    bool input_changed = false;

    std::lock_guard<std::mutex> lk(g_input_mutex);
    for (auto& it : items) {
        APItemID rel = it.item - g_cfg.item_base_id;
        if (rel < 0) continue;

        // ── Input unlock items: rel 0–8 ───────────────────────────────────────
        if (rel >= INPUT_ITEM_BASE && rel < INPUT_ITEM_BASE + INPUT_ITEM_COUNT) {
            const char* names[INPUT_ITEM_COUNT] = {
                "BT-A","BT-B","BT-C","BT-D","FX-L","FX-R","START",
                "Knob LEFT","Knob RIGHT"
            };
            log("[AP] Input unlocked: " + std::string(names[rel]) +
                " (item_id=" + std::to_string(it.item) + ")");

            switch (static_cast<InputSlot>(rel)) {
            case INPUT_BT_A:   g_gpio0_unlocked   |= SDVX_GPIO0_BT_A;  input_changed = true; break;
            case INPUT_BT_B:   g_gpio0_unlocked   |= SDVX_GPIO0_BT_B;  input_changed = true; break;
            case INPUT_BT_C:   g_gpio0_unlocked   |= SDVX_GPIO0_BT_C;  input_changed = true; break;
            case INPUT_BT_D:   g_gpio1_unlocked   |= SDVX_GPIO1_BT_D;  input_changed = true; break;
            case INPUT_FX_L:   g_gpio1_unlocked   |= SDVX_GPIO1_FX_L;  input_changed = true; break;
            case INPUT_FX_R:   g_gpio1_unlocked   |= SDVX_GPIO1_FX_R;  input_changed = true; break;
            case INPUT_START:  g_gpio0_unlocked   |= SDVX_GPIO0_START; input_changed = true; break;
            case INPUT_KNOB_L: g_spinner_unlocked |= SDVX_SPINNER_L;   input_changed = true; break;
            case INPUT_KNOB_R: g_spinner_unlocked |= SDVX_SPINNER_R;   input_changed = true; break;
            default: break;
            }
            continue;
        }

        // ── Clear type items: rel 20–24 ───────────────────────────────────────
        if (rel >= CLEAR_ITEM_BASE && rel < CLEAR_ITEM_BASE + CLEAR_ITEM_COUNT) {
            static const char* clear_names[CLEAR_ITEM_COUNT] = {
                "Clear", "Hard Clear", "Maxxive Clear", "UC", "PUC"
            };
            ++g_total_clears;
            log("[AP] " + std::string(clear_names[rel - CLEAR_ITEM_BASE]) + " received (" +
                std::to_string(g_total_clears) + " / " +
                std::to_string(g_cfg.goal_clears) + ")");
            if (!g_goal_sent && g_cfg.goal_mode == 0 &&
                g_total_clears >= g_cfg.goal_clears) {
                g_goal_sent = true;
                g_ap.send_goal();
                log("[AP] Goal reached (song_clears)!");
            }
            continue;
        }

        // ── Goal Song: rel 50 ─────────────────────────────────────────────────
        if (rel == GOAL_SONG_REL) {
            ++g_goal_songs;
            log("[AP] Goal Song received (" +
                std::to_string(g_goal_songs) + " / " +
                std::to_string(g_cfg.goal_song_count) + ")");
            if (!g_goal_sent && g_cfg.goal_mode == 1 &&
                g_goal_songs >= g_cfg.goal_song_count) {
                g_goal_sent = true;
                g_ap.send_goal();
                log("[AP] Goal reached (goal_songs)!");
            }
            continue;
        }

        // ── Level-folder unlock items: rel 101–120 ────────────────────────────
        {
            int level_rel = static_cast<int>(rel) - LEVEL_ITEM_BASE;
            if (level_rel >= 0 && level_rel < LEVEL_ITEM_COUNT) {
                int      level = level_rel + 1;
                uint32_t bit   = 1u << level_rel;
                if (!(g_levels_unlocked_local & bit)) {
                    g_levels_unlocked_local |= bit;
                    log("[AP] Level unlock: LEVEL " + std::to_string(level) +
                        " (item_id=" + std::to_string(it.item) + ")");
                    hooks_set_level_unlock(g_levels_unlocked_local);
                    ipc_write_dll_state();
                }
                continue;
            }
        }

        // ── Unknown/future items ──────────────────────────────────────────────
        log("[AP] Received unhandled item: rel=" + std::to_string(rel) +
            " (item_id=" + std::to_string(it.item) + ")");
    }

    if (input_changed)
        apply_input_lock();
}

static void on_ap_print(const std::string& msg) {
    log("[AP MSG] " + msg);
}

static void on_ap_connected(int slot, const std::string& name,
                             const std::unordered_map<std::string, int>& slot_data) {
    log("[AP] Connected as slot " + std::to_string(slot) + " (" + name + ")");

    auto apply = [&](const char* key, int& cfg_field) {
        auto it = slot_data.find(key);
        if (it == slot_data.end()) return;
        if (it->second != cfg_field)
            log(std::string("[AP] Slot data override: ") + key +
                " ini=" + std::to_string(cfg_field) +
                " -> server=" + std::to_string(it->second));
        cfg_field = it->second;
    };
    apply("goal_mode",       g_cfg.goal_mode);
    apply("goal_clears",     g_cfg.goal_clears);
    apply("goal_song_count", g_cfg.goal_song_count);

    if (g_shm) g_shm->ap_status = 2;

    // Re-send cleared locations
    {
        std::lock_guard<std::mutex> lk(g_cleared_mutex);
        if (!g_cleared_locations.empty()) {
            std::vector<APLocationID> locs(g_cleared_locations.begin(), g_cleared_locations.end());
            g_ap.send_location_checks(locs);
            log("[AP] Re-sent " + std::to_string(locs.size()) + " pending location check(s)");
        }
    }

    // Re-apply the current input lock state (hooks may have been reinstalled)
    {
        std::lock_guard<std::mutex> lk(g_input_mutex);
        apply_input_lock();
    }
}

// ============================================================
// Initialization (runs in a background thread from DllMain)
// ============================================================
static DWORD WINAPI init_thread(LPVOID) {
    // Give the game time to finish loading (dinputhook-sdvx.dll loads sdvxio
    // during its own DllMain, so by the time we reach here it's already done).
    Sleep(3000);

    // Find config next to the game exe
    char exe_path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    std::string dir(exe_path);
    auto slash = dir.find_last_of("\\/");
    if (slash != std::string::npos) dir = dir.substr(0, slash + 1);
    std::string ini_path = dir + "archipelago.ini";

    g_cfg = Config::load(ini_path);

    if (g_cfg.debug_log) {
        std::string log_path = dir + g_cfg.log_path;
        g_log.open(log_path, std::ios::trunc);
    }

    log("=== SDVX Archipelago DLL loaded ===");
    log("Config: host=" + g_cfg.ap_host +
        " port=" + std::to_string(g_cfg.ap_port) +
        " slot=" + g_cfg.ap_slot);

    // ── Debug UI: create shared memory so the debug app can always connect ──────
    // Created before the config check so the UI can reflect the "no config" state.
    {
        g_shm_handle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                          PAGE_READWRITE, 0, SDVX_AP_IPC_SIZE,
                                          SDVX_AP_SHM_NAME);
        if (g_shm_handle) {
            g_shm = reinterpret_cast<SdvxApSharedState*>(
                MapViewOfFile(g_shm_handle, FILE_MAP_ALL_ACCESS, 0, 0, SDVX_AP_IPC_SIZE));
            if (g_shm) {
                ZeroMemory(g_shm, SDVX_AP_IPC_SIZE);
                g_shm->magic     = SDVX_AP_IPC_MAGIC;
                g_shm->ap_status = 0;  // no config yet
                log("[IPC] Shared memory created: Local\\SDVX_AP_IPC_v1");
            }
        }
        if (!g_shm) log("[IPC] WARNING: Failed to create shared memory");

        // Start poll thread (runs even if IPC failed — it'll just do nothing)
        g_ipc_stop_evt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (g_ipc_stop_evt)
            g_ipc_thread = CreateThread(nullptr, 0, ipc_poll_thread, nullptr, 0, nullptr);
    }

    if (g_cfg.ap_slot.empty()) {
        log("ERROR: archipelago.ini missing or slot not set. AP disabled.");
        return 0;
    }

    // Install game hooks (avs2 score hook + sdvxio input lock hooks).
    // All inputs start locked (g_gpio0_unlocked = 0) until AP sends items.
    if (!hooks_install(on_session_result, on_track_clear, [](const std::string& s){ log(s); })) {
        log("ERROR: Failed to install game hooks");
        return 0;
    }
    log("Hooks installed OK — inputs locked, all LEVEL folders locked until AP items arrive");

    if (g_shm) g_shm->ap_status = 1;  // hooks installed, waiting for AP connection

    // Wire AP client callbacks
    g_ap.on_items_received = on_ap_items;
    g_ap.on_print          = on_ap_print;
    g_ap.on_connected      = on_ap_connected;
    g_ap.on_goal           = []{ log("[AP] Goal achieved!"); };

    // Start AP client
    g_ap.start(g_cfg.ap_host, g_cfg.ap_port,
               g_cfg.ap_game, g_cfg.ap_slot, g_cfg.ap_password);
    log("AP client started");

    return 0;
}

// ============================================================
// version.dll proxy — forward all exports to the real system version.dll
// ============================================================
static HMODULE g_real_version = nullptr;

static void* fp_GetFileVersionInfoA    = nullptr;
static void* fp_GetFileVersionInfoExA  = nullptr;
static void* fp_GetFileVersionInfoExW  = nullptr;
static void* fp_GetFileVersionInfoSizeA   = nullptr;
static void* fp_GetFileVersionInfoSizeExA = nullptr;
static void* fp_GetFileVersionInfoSizeExW = nullptr;
static void* fp_GetFileVersionInfoSizeW   = nullptr;
static void* fp_GetFileVersionInfoW    = nullptr;
static void* fp_VerFindFileA           = nullptr;
static void* fp_VerFindFileW           = nullptr;
static void* fp_VerInstallFileA        = nullptr;
static void* fp_VerInstallFileW        = nullptr;
static void* fp_VerLanguageNameA       = nullptr;
static void* fp_VerLanguageNameW       = nullptr;
static void* fp_VerQueryValueA         = nullptr;
static void* fp_VerQueryValueW         = nullptr;

static bool load_real_version() {
    char sys_path[MAX_PATH];
    GetSystemDirectoryA(sys_path, MAX_PATH);
    strcat_s(sys_path, "\\version.dll");
    g_real_version = LoadLibraryA(sys_path);
    if (!g_real_version) return false;

#define RESOLVE(fn) fp_##fn = GetProcAddress(g_real_version, #fn)
    RESOLVE(GetFileVersionInfoA);
    RESOLVE(GetFileVersionInfoExA);
    RESOLVE(GetFileVersionInfoExW);
    RESOLVE(GetFileVersionInfoSizeA);
    RESOLVE(GetFileVersionInfoSizeExA);
    RESOLVE(GetFileVersionInfoSizeExW);
    RESOLVE(GetFileVersionInfoSizeW);
    RESOLVE(GetFileVersionInfoW);
    RESOLVE(VerFindFileA);
    RESOLVE(VerFindFileW);
    RESOLVE(VerInstallFileA);
    RESOLVE(VerInstallFileW);
    RESOLVE(VerLanguageNameA);
    RESOLVE(VerLanguageNameW);
    RESOLVE(VerQueryValueA);
    RESOLVE(VerQueryValueW);
#undef RESOLVE
    return true;
}

// Exported proxy functions — named ver_fwd_* to avoid colliding with the
// winver.h declarations already pulled in by windows.h.  The .def file
// re-exports them under the canonical version.dll names.
extern "C" {

BOOL  WINAPI ver_fwd_GetFileVersionInfoA(LPCSTR f, DWORD h, DWORD l, LPVOID d) {
    return reinterpret_cast<BOOL(WINAPI*)(LPCSTR,DWORD,DWORD,LPVOID)>(fp_GetFileVersionInfoA)(f,h,l,d);
}
BOOL  WINAPI ver_fwd_GetFileVersionInfoW(LPCWSTR f, DWORD h, DWORD l, LPVOID d) {
    return reinterpret_cast<BOOL(WINAPI*)(LPCWSTR,DWORD,DWORD,LPVOID)>(fp_GetFileVersionInfoW)(f,h,l,d);
}
DWORD WINAPI ver_fwd_GetFileVersionInfoSizeA(LPCSTR f, LPDWORD h) {
    return reinterpret_cast<DWORD(WINAPI*)(LPCSTR,LPDWORD)>(fp_GetFileVersionInfoSizeA)(f,h);
}
DWORD WINAPI ver_fwd_GetFileVersionInfoSizeW(LPCWSTR f, LPDWORD h) {
    return reinterpret_cast<DWORD(WINAPI*)(LPCWSTR,LPDWORD)>(fp_GetFileVersionInfoSizeW)(f,h);
}
BOOL  WINAPI ver_fwd_GetFileVersionInfoExA(DWORD flags, LPCSTR f, DWORD h, DWORD l, LPVOID d) {
    return reinterpret_cast<BOOL(WINAPI*)(DWORD,LPCSTR,DWORD,DWORD,LPVOID)>(fp_GetFileVersionInfoExA)(flags,f,h,l,d);
}
BOOL  WINAPI ver_fwd_GetFileVersionInfoExW(DWORD flags, LPCWSTR f, DWORD h, DWORD l, LPVOID d) {
    return reinterpret_cast<BOOL(WINAPI*)(DWORD,LPCWSTR,DWORD,DWORD,LPVOID)>(fp_GetFileVersionInfoExW)(flags,f,h,l,d);
}
DWORD WINAPI ver_fwd_GetFileVersionInfoSizeExA(DWORD flags, LPCSTR f, LPDWORD h) {
    return reinterpret_cast<DWORD(WINAPI*)(DWORD,LPCSTR,LPDWORD)>(fp_GetFileVersionInfoSizeExA)(flags,f,h);
}
DWORD WINAPI ver_fwd_GetFileVersionInfoSizeExW(DWORD flags, LPCWSTR f, LPDWORD h) {
    return reinterpret_cast<DWORD(WINAPI*)(DWORD,LPCWSTR,LPDWORD)>(fp_GetFileVersionInfoSizeExW)(flags,f,h);
}
DWORD WINAPI ver_fwd_VerFindFileA(DWORD f, LPSTR fn, LPSTR wd, LPSTR ad, LPSTR cd, PUINT cl, LPSTR dd, PUINT dl) {
    return reinterpret_cast<DWORD(WINAPI*)(DWORD,LPSTR,LPSTR,LPSTR,LPSTR,PUINT,LPSTR,PUINT)>(fp_VerFindFileA)(f,fn,wd,ad,cd,cl,dd,dl);
}
DWORD WINAPI ver_fwd_VerFindFileW(DWORD f, LPWSTR fn, LPWSTR wd, LPWSTR ad, LPWSTR cd, PUINT cl, LPWSTR dd, PUINT dl) {
    return reinterpret_cast<DWORD(WINAPI*)(DWORD,LPWSTR,LPWSTR,LPWSTR,LPWSTR,PUINT,LPWSTR,PUINT)>(fp_VerFindFileW)(f,fn,wd,ad,cd,cl,dd,dl);
}
DWORD WINAPI ver_fwd_VerInstallFileA(DWORD f, LPSTR sf, LPSTR df, LPSTR sd, LPSTR dd, LPSTR cd, LPSTR tf, PUINT tl) {
    return reinterpret_cast<DWORD(WINAPI*)(DWORD,LPSTR,LPSTR,LPSTR,LPSTR,LPSTR,LPSTR,PUINT)>(fp_VerInstallFileA)(f,sf,df,sd,dd,cd,tf,tl);
}
DWORD WINAPI ver_fwd_VerInstallFileW(DWORD f, LPWSTR sf, LPWSTR df, LPWSTR sd, LPWSTR dd, LPWSTR cd, LPWSTR tf, PUINT tl) {
    return reinterpret_cast<DWORD(WINAPI*)(DWORD,LPWSTR,LPWSTR,LPWSTR,LPWSTR,LPWSTR,LPWSTR,PUINT)>(fp_VerInstallFileW)(f,sf,df,sd,dd,cd,tf,tl);
}
DWORD WINAPI ver_fwd_VerLanguageNameA(DWORD lang, LPSTR buf, DWORD len) {
    return reinterpret_cast<DWORD(WINAPI*)(DWORD,LPSTR,DWORD)>(fp_VerLanguageNameA)(lang,buf,len);
}
DWORD WINAPI ver_fwd_VerLanguageNameW(DWORD lang, LPWSTR buf, DWORD len) {
    return reinterpret_cast<DWORD(WINAPI*)(DWORD,LPWSTR,DWORD)>(fp_VerLanguageNameW)(lang,buf,len);
}
BOOL  WINAPI ver_fwd_VerQueryValueA(LPCVOID blk, LPCSTR sub, LPVOID* buf, PUINT len) {
    return reinterpret_cast<BOOL(WINAPI*)(LPCVOID,LPCSTR,LPVOID*,PUINT)>(fp_VerQueryValueA)(blk,sub,buf,len);
}
BOOL  WINAPI ver_fwd_VerQueryValueW(LPCVOID blk, LPCWSTR sub, LPVOID* buf, PUINT len) {
    return reinterpret_cast<BOOL(WINAPI*)(LPCVOID,LPCWSTR,LPVOID*,PUINT)>(fp_VerQueryValueW)(blk,sub,buf,len);
}

} // extern "C"

// ============================================================
// DllMain
// ============================================================
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);
        load_real_version();

        // Load dinputhook-sdvx.dll if it's sitting alongside us in the game
        // folder.  Its DllMain installs the DirectInput8Create hook and calls
        // sdvx_io_init, which loads sdvxio.dll.  This must happen before the
        // game initialises DirectInput (i.e. here in DllMain, not in our
        // delayed init_thread).  LoadLibraryA returns NULL silently if the
        // file doesn't exist — that's fine; sdvxio hooks are optional.
        LoadLibraryA("dinputhook-sdvx.dll");

        // Spin up init on its own thread so DllMain returns quickly
        CloseHandle(CreateThread(nullptr, 0, init_thread, nullptr, 0, nullptr));
    }
    else if (reason == DLL_PROCESS_DETACH) {
        g_ap.stop();
        hooks_remove();

        // Tear down IPC
        if (g_ipc_stop_evt) {
            SetEvent(g_ipc_stop_evt);
            if (g_ipc_thread) {
                WaitForSingleObject(g_ipc_thread, 500);
                CloseHandle(g_ipc_thread);
                g_ipc_thread = nullptr;
            }
            CloseHandle(g_ipc_stop_evt);
            g_ipc_stop_evt = nullptr;
        }
        if (g_shm)        { UnmapViewOfFile(g_shm);     g_shm        = nullptr; }
        if (g_shm_handle) { CloseHandle(g_shm_handle);  g_shm_handle = nullptr; }

        if (g_real_version) {
            FreeLibrary(g_real_version);
            g_real_version = nullptr;
        }
    }
    return TRUE;
}
