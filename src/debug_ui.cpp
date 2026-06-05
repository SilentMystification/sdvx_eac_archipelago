// sdvx_ap_debug.exe — Touch-friendly debug UI for SDVX Archipelago
// Communicates with version.dll via named shared memory (Local\SDVX_AP_IPC_v1).
// Build: cl /O2 /MT /W3 /EHsc /std:c++17 /DUNICODE /D_UNICODE
//        debug_ui.cpp /Fe"sdvx_ap_debug.exe"
//        /link kernel32.lib user32.lib gdi32.lib

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>

// ── IPC (inline, no header dependency) ───────────────────────────────────────
#define SDVX_AP_SHM_NAME   L"Local\\SDVX_AP_IPC_v1"
constexpr uint32_t SDVX_AP_IPC_MAGIC = 0xAC8E0001u;
constexpr SIZE_T   SDVX_AP_IPC_SIZE  = 64u;

#pragma pack(push,1)
struct SdvxApSharedState {
    uint32_t          magic;
    uint16_t          inputs;
    uint8_t           ap_status;  // 0=no config, 1=hooks installed, 2=AP connected
    uint8_t           _pad0;
    uint32_t          levels;
    volatile uint32_t seq_dll;
    volatile uint32_t seq_ui;
};
#pragma pack(pop)

// ── Layout constants ──────────────────────────────────────────────────────────
static constexpr int WIN_W    = 594;
static constexpr int WIN_H    = 730;
static constexpr int MARGIN   = 12;
static constexpr int GAP      = 8;

// Input buttons: 3 rows of 3 (BT-A BT-B BT-C / BT-D FX-L FX-R / START KL KR)
static constexpr int I_BTN_W  = 178;
static constexpr int I_BTN_H  = 90;

// Level buttons: 4 rows of 5 (L1-5 / L6-10 / L11-15 / L16-20)
static constexpr int L_BTN_W  = 104;
static constexpr int L_BTN_H  = 56;

// Utility buttons: Unlock All / Lock All
static constexpr int U_BTN_W  = 279;
static constexpr int U_BTN_H  = 44;

// Status bar height
static constexpr int STATUS_H = 28;

// ── Colours ───────────────────────────────────────────────────────────────────
static constexpr COLORREF CLR_BG       = RGB(17, 17, 17);
static constexpr COLORREF CLR_TEXT     = RGB(230, 230, 230);
static constexpr COLORREF CLR_DIM      = RGB(55, 55, 55);   // locked button face
static constexpr COLORREF CLR_DIM_TXT  = RGB(100, 100, 100);

// Input button colours (unlocked state)
static constexpr COLORREF CLR_BT       = RGB(0,  120, 215);  // BT-A/B/C/D
static constexpr COLORREF CLR_FX       = RGB(225, 100,  0);  // FX-L/R
static constexpr COLORREF CLR_START    = RGB(0,  155, 55);   // START
static constexpr COLORREF CLR_KNOB     = RGB(148,  28, 196); // Knobs

// Level button colours by tier
static constexpr COLORREF CLR_LVL[4]  = {
    RGB(0, 148, 155),   // L1-5   teal
    RGB(0,  80, 200),   // L6-10  blue
    RGB(200, 90,   0),  // L11-15 orange
    RGB(190,  20,  20), // L16-20 red
};

static constexpr COLORREF CLR_UNLOCK_ALL = RGB(0, 155, 55);
static constexpr COLORREF CLR_LOCK_ALL   = RGB(160, 20, 20);

static constexpr COLORREF CLR_CONNECTED    = RGB(0, 200, 80);
static constexpr COLORREF CLR_DISCONNECTED = RGB(200, 50, 50);

// ── Button descriptor ─────────────────────────────────────────────────────────
struct Button {
    RECT      rc;
    const wchar_t* label;
    int       id;        // unique id for hit-test
    COLORREF  clr_on;    // colour when unlocked/active
    bool      is_toggle; // true = input/level toggle; false = action btn
    bool      state;     // current on/off state (toggles only)
};

static constexpr int BTN_INPUT_BASE  = 0;    // ids 0-8  (input buttons)
static constexpr int BTN_LEVEL_BASE  = 100;  // ids 100-119 (level buttons)
static constexpr int BTN_UNLOCK_ALL  = 200;
static constexpr int BTN_LOCK_ALL    = 201;

static constexpr int INPUT_COUNT = 9;
static constexpr int LEVEL_COUNT = 20;
static constexpr int TOTAL_BUTTONS = INPUT_COUNT + LEVEL_COUNT + 2;

static Button g_btns[TOTAL_BUTTONS];
static int    g_btn_count = 0;

// ── IPC state ─────────────────────────────────────────────────────────────────
static HANDLE              g_shm_handle   = nullptr;
static SdvxApSharedState*  g_shm          = nullptr;
static uint32_t            g_last_seq_dll = 0;

static bool ipc_open() {
    if (g_shm) return true;
    g_shm_handle = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, SDVX_AP_SHM_NAME);
    if (!g_shm_handle) return false;
    g_shm = reinterpret_cast<SdvxApSharedState*>(
        MapViewOfFile(g_shm_handle, FILE_MAP_ALL_ACCESS, 0, 0, SDVX_AP_IPC_SIZE));
    if (!g_shm || g_shm->magic != SDVX_AP_IPC_MAGIC) {
        if (g_shm) { UnmapViewOfFile(g_shm); g_shm = nullptr; }
        if (g_shm_handle) { CloseHandle(g_shm_handle); g_shm_handle = nullptr; }
        return false;
    }
    g_last_seq_dll = g_shm->seq_dll;
    return true;
}

static void ipc_close() {
    if (g_shm)        { UnmapViewOfFile(g_shm);      g_shm        = nullptr; }
    if (g_shm_handle) { CloseHandle(g_shm_handle);   g_shm_handle = nullptr; }
}

// Read back DLL-side state into button toggle states.
static void ipc_read_state() {
    if (!g_shm) return;
    uint16_t inp = g_shm->inputs;
    uint32_t lvl = g_shm->levels;
    for (int i = 0; i < INPUT_COUNT; i++)
        g_btns[BTN_INPUT_BASE + i].state = (inp >> i) & 1;
    for (int i = 0; i < LEVEL_COUNT; i++)
        g_btns[INPUT_COUNT + i].state = (lvl >> i) & 1;
}

// Write current button states to shared memory and bump seq_ui.
static void ipc_write_state() {
    if (!g_shm) return;
    uint16_t inp = 0;
    uint32_t lvl = 0;
    for (int i = 0; i < INPUT_COUNT; i++)
        if (g_btns[i].state) inp |= (uint16_t)(1u << i);
    for (int i = 0; i < LEVEL_COUNT; i++)
        if (g_btns[INPUT_COUNT + i].state) lvl |= (1u << i);
    g_shm->inputs = inp;
    g_shm->levels = lvl;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_shm->seq_ui));
}

// ── Button layout builder ─────────────────────────────────────────────────────
static void build_layout() {
    g_btn_count = 0;
    auto add = [&](int x, int y, int w, int h,
                   const wchar_t* lbl, int id, COLORREF clr, bool toggle) {
        Button& b = g_btns[g_btn_count++];
        b.rc = {x, y, x+w, y+h};
        b.label    = lbl;
        b.id       = id;
        b.clr_on   = clr;
        b.is_toggle= toggle;
        b.state    = false;
    };

    // ── Row 0: section header "INPUTS" is drawn by WM_PAINT; buttons start below
    int sec_label_h = 22;

    // Input buttons — 3 columns × 3 rows
    int ix0 = MARGIN;
    int iy0 = MARGIN + sec_label_h;

    // Row 1: BT-A  BT-B  BT-C
    add(ix0,                    iy0,                 I_BTN_W, I_BTN_H, L"BT-A",       0, CLR_BT,    true);
    add(ix0 + I_BTN_W + GAP,    iy0,                 I_BTN_W, I_BTN_H, L"BT-B",       1, CLR_BT,    true);
    add(ix0 + (I_BTN_W+GAP)*2,  iy0,                 I_BTN_W, I_BTN_H, L"BT-C",       2, CLR_BT,    true);

    // Row 2: BT-D  FX-L  FX-R
    add(ix0,                    iy0 + I_BTN_H + GAP, I_BTN_W, I_BTN_H, L"BT-D",       3, CLR_BT,    true);
    add(ix0 + I_BTN_W + GAP,    iy0 + I_BTN_H + GAP, I_BTN_W, I_BTN_H, L"FX-L",       4, CLR_FX,    true);
    add(ix0 + (I_BTN_W+GAP)*2,  iy0 + I_BTN_H + GAP, I_BTN_W, I_BTN_H, L"FX-R",       5, CLR_FX,    true);

    // Row 3: START  Knob L  Knob R
    add(ix0,                    iy0 + (I_BTN_H+GAP)*2, I_BTN_W, I_BTN_H, L"START",     6, CLR_START, true);
    add(ix0 + I_BTN_W + GAP,    iy0 + (I_BTN_H+GAP)*2, I_BTN_W, I_BTN_H, L"Knob L",   7, CLR_KNOB,  true);
    add(ix0 + (I_BTN_W+GAP)*2,  iy0 + (I_BTN_H+GAP)*2, I_BTN_W, I_BTN_H, L"Knob R",  8, CLR_KNOB,  true);

    // ── Level buttons — 4 rows × 5 columns
    int ly0 = iy0 + (I_BTN_H + GAP) * 3 + sec_label_h + GAP * 2;
    static const wchar_t* lvl_labels[20] = {
        L"LV 1",  L"LV 2",  L"LV 3",  L"LV 4",  L"LV 5",
        L"LV 6",  L"LV 7",  L"LV 8",  L"LV 9",  L"LV 10",
        L"LV 11", L"LV 12", L"LV 13", L"LV 14", L"LV 15",
        L"LV 16", L"LV 17", L"LV 18", L"LV 19", L"LV 20",
    };
    for (int i = 0; i < 20; i++) {
        int col  = i % 5;
        int row  = i / 5;
        int tier = row;   // 0-3
        int x    = MARGIN + col * (L_BTN_W + GAP);
        int y    = ly0    + row * (L_BTN_H + GAP);
        add(x, y, L_BTN_W, L_BTN_H, lvl_labels[i],
            BTN_LEVEL_BASE + i, CLR_LVL[tier], true);
    }

    // ── Utility buttons
    int uy0 = ly0 + 4 * (L_BTN_H + GAP) + GAP;
    add(MARGIN,              uy0, U_BTN_W, U_BTN_H, L"Unlock All", BTN_UNLOCK_ALL, CLR_UNLOCK_ALL, false);
    add(MARGIN + U_BTN_W + GAP, uy0, U_BTN_W, U_BTN_H, L"Lock All",   BTN_LOCK_ALL,   CLR_LOCK_ALL,   false);
}

// ── Drawing ───────────────────────────────────────────────────────────────────
static HFONT g_font_btn  = nullptr;
static HFONT g_font_sec  = nullptr;
static HFONT g_font_stat = nullptr;

static void create_fonts() {
    g_font_btn  = CreateFontW(-22, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH, L"Segoe UI");
    g_font_sec  = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH, L"Segoe UI");
    g_font_stat = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH, L"Segoe UI");
}

// Draw a single button into an already-selected DC.
static void draw_button(HDC hdc, const Button& b) {
    bool on = b.state;
    COLORREF face = on ? b.clr_on : CLR_DIM;
    COLORREF txt  = on ? CLR_TEXT : CLR_DIM_TXT;

    // Fill
    HBRUSH br = CreateSolidBrush(face);
    FillRect(hdc, &b.rc, br);
    DeleteObject(br);

    // Rounded-ish border
    HPEN pen = CreatePen(PS_SOLID, 1, on ? RGB(255,255,255) : RGB(70,70,70));
    HPEN old_pen = (HPEN)SelectObject(hdc, pen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, b.rc.left, b.rc.top, b.rc.right, b.rc.bottom, 8, 8);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);

    // Label
    SelectObject(hdc, g_font_btn);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, txt);
    DrawTextW(hdc, b.label, -1, const_cast<RECT*>(&b.rc),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void draw_section_label(HDC hdc, int x, int y, const wchar_t* text) {
    SelectObject(hdc, g_font_sec);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(150, 150, 150));
    RECT r = {x, y, x + 400, y + 20};
    DrawTextW(hdc, text, -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE);
}

static void paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc_real = BeginPaint(hwnd, &ps);

    // Double buffer
    RECT cr; GetClientRect(hwnd, &cr);
    HDC hdc = CreateCompatibleDC(hdc_real);
    HBITMAP bmp = CreateCompatibleBitmap(hdc_real, cr.right, cr.bottom);
    HBITMAP old_bmp = (HBITMAP)SelectObject(hdc, bmp);

    // Background
    HBRUSH bg = CreateSolidBrush(CLR_BG);
    FillRect(hdc, &cr, bg);
    DeleteObject(bg);

    int sec_label_h = 22;

    // "INPUTS" section label
    draw_section_label(hdc, MARGIN, MARGIN + 3, L"INPUTS");

    // Input buttons
    for (int i = 0; i < INPUT_COUNT; i++)
        draw_button(hdc, g_btns[i]);

    // "LEVEL FOLDERS" section label — just above level buttons
    int ly0 = g_btns[INPUT_COUNT - 1].rc.bottom + GAP * 2 + 4;
    // (Note: first level button is at g_btns[INPUT_COUNT])
    int label_y = g_btns[INPUT_COUNT].rc.top - sec_label_h - 2;
    draw_section_label(hdc, MARGIN, label_y, L"LEVEL FOLDERS");

    // Level + utility buttons
    for (int i = INPUT_COUNT; i < g_btn_count; i++)
        draw_button(hdc, g_btns[i]);

    // Status bar
    bool connected = (g_shm != nullptr);
    RECT status_rc = {0, cr.bottom - STATUS_H, cr.right, cr.bottom};
    HBRUSH sbr = CreateSolidBrush(RGB(28, 28, 28));
    FillRect(hdc, &status_rc, sbr);
    DeleteObject(sbr);

    // Status line separator
    HPEN sep = CreatePen(PS_SOLID, 1, RGB(50,50,50));
    HPEN old = (HPEN)SelectObject(hdc, sep);
    MoveToEx(hdc, 0, status_rc.top, nullptr);
    LineTo(hdc, cr.right, status_rc.top);
    SelectObject(hdc, old); DeleteObject(sep);

    SelectObject(hdc, g_font_stat);
    SetBkMode(hdc, TRANSPARENT);
    if (!connected) {
        SetTextColor(hdc, CLR_DISCONNECTED);
        DrawTextW(hdc, L"○  Game not running", -1, &status_rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        uint8_t ap_st = g_shm->ap_status;
        if (ap_st == 0) {
            SetTextColor(hdc, RGB(220, 140, 0));
            DrawTextW(hdc, L"○  Game running — configure archipelago.ini", -1, &status_rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else if (ap_st == 1) {
            SetTextColor(hdc, RGB(220, 200, 0));
            DrawTextW(hdc, L"●  Game connected — AP disconnected", -1, &status_rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            SetTextColor(hdc, CLR_CONNECTED);
            DrawTextW(hdc, L"●  Game connected — AP connected", -1, &status_rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    BitBlt(hdc_real, 0, 0, cr.right, cr.bottom, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, old_bmp);
    DeleteObject(bmp);
    DeleteDC(hdc);

    EndPaint(hwnd, &ps);
}

// ── Window proc ───────────────────────────────────────────────────────────────
static HWND g_hwnd = nullptr;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_LBUTTONUP: {
        int mx = LOWORD(lp), my = HIWORD(lp);
        for (int i = 0; i < g_btn_count; i++) {
            Button& b = g_btns[i];
            if (mx >= b.rc.left && mx < b.rc.right &&
                my >= b.rc.top  && my < b.rc.bottom) {
                if (b.id == BTN_UNLOCK_ALL) {
                    for (int j = 0; j < g_btn_count - 2; j++) g_btns[j].state = true;
                } else if (b.id == BTN_LOCK_ALL) {
                    for (int j = 0; j < g_btn_count - 2; j++) g_btns[j].state = false;
                } else if (b.is_toggle) {
                    b.state = !b.state;
                }
                ipc_write_state();
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            }
        }
        return 0;
    }
    case WM_TIMER: {
        if (wp == 1) {
            bool was_connected = (g_shm != nullptr);
            if (!g_shm) ipc_open();
            else if (g_shm->magic != SDVX_AP_IPC_MAGIC) { ipc_close(); }

            bool now_connected = (g_shm != nullptr);

            // If DLL wrote new state, sync buttons to it
            if (g_shm) {
                uint32_t dll_seq = g_shm->seq_dll;
                if (dll_seq != g_last_seq_dll) {
                    g_last_seq_dll = dll_seq;
                    ipc_read_state();
                }
            }

            if (was_connected != now_connected)
                InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_PAINT:
        paint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;   // handled in WM_PAINT
    case WM_GETMINMAXINFO: {
        auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
        mm->ptMinTrackSize = {WIN_W, WIN_H};
        mm->ptMaxTrackSize = {WIN_W, WIN_H};
        return 0;
    }
    case WM_DESTROY:
        ipc_close();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Entry point ───────────────────────────────────────────────────────────────
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    // Single-instance guard
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"SDVX_AP_DebugUI_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Bring existing window to foreground
        HWND existing = FindWindowW(L"SdvxApDebugUI", nullptr);
        if (existing) { ShowWindow(existing, SW_RESTORE); SetForegroundWindow(existing); }
        return 0;
    }

    SetProcessDPIAware();

    build_layout();
    create_fonts();

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(CLR_BG);
    wc.lpszClassName = L"SdvxApDebugUI";
    RegisterClassExW(&wc);

    // Compute window size from client size
    RECT cr = {0, 0, WIN_W, WIN_H};
    AdjustWindowRect(&cr, WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX, FALSE);
    int win_w = cr.right  - cr.left;
    int win_h = cr.bottom - cr.top;

    g_hwnd = CreateWindowExW(0, L"SdvxApDebugUI",
                              L"SDVX AP Debug",
                              WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
                              CW_USEDEFAULT, CW_USEDEFAULT, win_w, win_h,
                              nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;

    ShowWindow(g_hwnd, nShow);
    UpdateWindow(g_hwnd);

    // Start 100ms poll timer
    SetTimer(g_hwnd, 1, 100, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_font_btn)  DeleteObject(g_font_btn);
    if (g_font_sec)  DeleteObject(g_font_sec);
    if (g_font_stat) DeleteObject(g_font_stat);
    if (mtx)         CloseHandle(mtx);
    return (int)msg.wParam;
}
