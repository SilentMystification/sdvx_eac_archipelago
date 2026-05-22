#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <mutex>
#include <set>
#include <cstdint>

#include "config.h"
#include "hooks.h"
#include "ap_client.h"
#include "offsets.h"

// ============================================================
// Globals
// ============================================================
static Config    g_cfg;
static APClient  g_ap;

static std::mutex             g_cleared_mutex;
static std::set<APLocationID> g_cleared_locations; // already sent this session
static int                    g_total_clears = 0;
static bool                   g_goal_sent    = false;

static std::ofstream g_log;
static std::mutex    g_log_mutex;

// ── Input unlock state ────────────────────────────────────────────────────────
// Input items are encoded as: item_id = item_base_id + INPUT_ITEM_OFFSET + slot
// Slots 0-6 = BT-A…START (GPIO0 bits), slots 7-8 = Knob L / Knob R.
static constexpr APItemID INPUT_ITEM_OFFSET = 100000;

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
static void on_track_clear(uint32_t music_id, uint32_t difficulty) {
    if (music_id == 0) {
        log("[HOOK] track clear fired but music_id=0");
        return;
    }

    APLocationID loc_id = make_location_id(music_id, difficulty);

    {
        std::lock_guard<std::mutex> lk(g_cleared_mutex);
        if (g_cleared_locations.count(loc_id)) return;
        g_cleared_locations.insert(loc_id);
    }

    log("[AP] Track cleared: music_id=" + std::to_string(music_id) +
        " diff=" + std::to_string(difficulty) +
        " location_id=" + std::to_string(loc_id));

    if (g_ap.is_connected()) {
        g_ap.send_location_checks({loc_id});
        g_total_clears++;
        log("[AP] Total clears: " + std::to_string(g_total_clears));

        if (!g_goal_sent && g_total_clears >= g_cfg.goal_clears) {
            g_goal_sent = true;
            g_ap.send_goal();
            log("[AP] GOAL SENT!");
        }
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

// Helper — recompute the input lock from the accumulated unlock masks and push
// it to hooks.cpp.  Call under g_input_mutex or after updating the masks.
static void apply_input_lock() {
    hooks_set_input_lock(g_gpio0_unlocked, g_gpio1_unlocked, g_spinner_unlocked);
}

static void on_ap_items(const std::vector<APItem>& items) {
    bool input_changed = false;

    std::lock_guard<std::mutex> lk(g_input_mutex);
    for (auto& it : items) {
        APItemID rel = it.item - g_cfg.item_base_id;
        if (rel < 0) continue;

        // ── Input unlock items ────────────────────────────────────────────────
        APItemID input_rel = it.item - (g_cfg.item_base_id + INPUT_ITEM_OFFSET);
        if (input_rel >= 0 && input_rel < INPUT_COUNT) {
            const char* names[INPUT_COUNT] = {
                "BT-A","BT-B","BT-C","BT-D","FX-L","FX-R","START",
                "Knob LEFT","Knob RIGHT"
            };
            log("[AP] Input unlocked: " + std::string(names[input_rel]) +
                " (item_id=" + std::to_string(it.item) + ")");

            switch (static_cast<InputSlot>(input_rel)) {
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
            continue; // not a song item
        }

        // ── Song unlock items ─────────────────────────────────────────────────
        uint32_t music_id   = (uint32_t)(rel / 10);
        uint32_t difficulty = (uint32_t)(rel % 10);
        log("[AP] Received song item: music_id=" + std::to_string(music_id) +
            " diff=" + std::to_string(difficulty) +
            " (item_id=" + std::to_string(it.item) + ")");

        // TODO: unlock song in game's unlock table (requires RE of unlock array)
    }

    if (input_changed)
        apply_input_lock();
}

static void on_ap_print(const std::string& msg) {
    log("[AP MSG] " + msg);
}

static void on_ap_connected(int slot, const std::string& name) {
    log("[AP] Connected as slot " + std::to_string(slot) + " (" + name + ")");

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
        g_log.open(log_path, std::ios::app);
    }

    log("=== SDVX Archipelago DLL loaded ===");
    log("Config: host=" + g_cfg.ap_host +
        " port=" + std::to_string(g_cfg.ap_port) +
        " slot=" + g_cfg.ap_slot);

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
    log("Hooks installed OK — inputs locked until AP items arrive");

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
        if (g_real_version) {
            FreeLibrary(g_real_version);
            g_real_version = nullptr;
        }
    }
    return TRUE;
}
