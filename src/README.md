# src/

Source files for the two compiled outputs: `version.dll` (game mod) and `sdvx_ap_debug.exe` (debug UI).

---

## version.dll sources

| File | Purpose |
|------|---------|
| [dllmain.cpp](dllmain.cpp) | DLL entry point. Loads the real `version.dll` proxy, creates named shared memory, reads config, starts the AP client, and wires hook callbacks to AP item/location logic. |
| [hooks.cpp](hooks.cpp) / [hooks.h](hooks.h) | MinHook hooks into `sdvxio.dll` (input gating via GPIO hooks) and `avs2-core.dll` (`property_destroy` intercept for detecting song clears). Public API: `hooks_install`, `hooks_remove`, `hooks_set_input_lock`, `hooks_set_level_unlock`. |
| [ap_client.cpp](ap_client.cpp) / [ap_client.h](ap_client.h) | Archipelago WebSocket client (WinHTTP). Connects, authenticates, sends location checks, receives items, and fires callbacks into `dllmain.cpp`. |
| [config.cpp](config.cpp) / [config.h](config.h) | Reads `archipelago.ini` via `GetPrivateProfileStringA`. Silently returns defaults for missing keys. Exposes a plain `Config` struct. |
| [ipc.h](ipc.h) | Defines `SdvxApSharedState` — the packed struct written to named shared memory (`Local\SDVX_AP_IPC_v1`). Shared between `version.dll` and `sdvx_ap_debug.exe`. |
| [offsets.h](offsets.h) | `RVA()` helper that resolves a relative virtual address against `sv6c.exe`'s loaded base, used when locating hook targets. |
| [exports.def](exports.def) | MSVC linker `.def` file listing all `version.dll` exports required for the proxy pattern (forwards every real `version.dll` function). |

## sdvx_ap_debug.exe sources

| File | Purpose |
|------|---------|
| [debug_ui.cpp](debug_ui.cpp) | Standalone Win32 GUI. Opens the shared memory segment written by `version.dll` and displays live button/knob unlock state, song difficulty unlocks, and AP connection status. Contains an inline copy of `SdvxApSharedState` that must stay in sync with [ipc.h](ipc.h). |
