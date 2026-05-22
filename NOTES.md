# SDVX Archipelago DLL — Investigation Notes
Last updated: 2026-05-22

## Project Goal
Inject a d3d9.dll proxy into sv6c.exe (Sound Voltex Exceed Gear, Konasute PC build) that:
1. Detects when a player clears a song
2. Sends a location check to an Archipelago multiworld server
3. Receives item unlocks (songs) from the AP server

## Repo Layout
```
sdvx_archipelago/
  build.bat              — MSVC build script (outputs build/d3d9.dll)
  src/
    dllmain.cpp          — D3D9 proxy + AP orchestration
    hooks.cpp            — MinHook function hooks into sv6c.exe
    hooks.h
    ap_client.cpp/h      — WinHTTP WebSocket AP protocol client
    config.cpp/h         — archipelago.ini reader
    offsets.h            — All RE-derived RVAs
    exports.def          — 9 d3d9 exports for the proxy
  MinHook/               — x64 trampoline hooking library
  include/               — nlohmann/json single-header
```

## Build
- MSVC 14.51.36231, SDK 10.0.26100.0, VS2022 Community
- Run `build.bat` from the repo root — outputs `build/d3d9.dll`
- Copy `build/d3d9.dll` to the folder containing `sv6c.exe`
- Config file: `archipelago.ini` in same folder as `sv6c.exe`

## DLL Injection Method
d3d9 proxy: Windows loads `d3d9.dll` from the exe's directory before System32.
Our DLL forwards all 9 D3D9 exports to the real `C:\Windows\System32\d3d9.dll`.
No separate injector needed — just drop the file next to `sv6c.exe`.

## sv6c.exe Binary Info
- Image base: `0x140000000`
- 64-bit PE, x86-64
- Version: SDVX Exceed Gear Konasute, file date 2026-05-20 (Ghidra)
- ~18K functions

## Known Offsets (offsets.h)
```
RVA_RESULT_UPLOAD  = 0x3FF640   FUN_1403ff640 — e-amusement session submit
RVA_CLEAR_STATE    = 0x29E620   FUN_14029e620 — returns "clear"/"normal"/"" string
RVA_STATE_DISPATCH = 0x4031F0   FUN_1404031f0 — BM2D state machine
RVA_RESULT_STATE   = 0xEF3E60   DAT_140ef3e60 — global ptr (see status below)
RVA_PROP_GETTER    = 0x611958   DAT_140611958 — BM2D property fn ptr
```

## What Works
- DLL loads correctly, hooks install OK
- `hk_clear_state` fires when the result screen appears (confirmed by log)
- AP WebSocket client connects, sends/receives JSON correctly
- Location check sending + goal detection logic is correct
- All 9 D3D9 exports forward correctly

## Current Problem: music_id = 0

### Symptom
`hk_clear_state` fires (FUN_14029e620 hook), detects "clear" string, but
cannot read the music_id. All attempts return 0.

### What We've Tried
1. **BM2D property getter** — reads `mc_handle` from `movieclip_ctx + 0x200`,
   calls the prop getter with "music_id". Returns 0; mc_handle is garbage
   (1590954929 = 0x5EBFDB71, not a valid handle). Wrong offset for mc_handle.

2. **Global result struct** (RVA_RESULT_STATE = 0xEF3E60) — pointer resolves to
   `0x68D38E50` at runtime, but the entire 512-byte struct is **all zeros** when
   the hook fires. The struct hasn't been populated yet at this point.

### Key Diagnostic Log (2026-05-22 02:23:29)
```
[HOOK] clear_state[0]: ctx=000000007F204010 base=0000000068D38E50 str="clear" is_clear=1
[STRUCT] RVA_RESULT_STATE addr=0x140EF3E60 value=0000000068D38E50
[STRUCT] --- all zeros ---
[HOOK] BM2D mc=1590954929 → music_id=0 diff=0
```

### Root Cause Hypothesis
`FUN_14029e620` is called for **UI queries** ("has this song ever been cleared?")
as well as live session results. When called for UI, the result struct isn't
populated yet. We're hooking too early — the struct is written later by a
different codepath.

### Cheat Engine Findings
Score is written live to this heap address (changes between songs in a session):
```
5DCFD0C8   ← confirmed score, updated after clearing each song
```
Also found (all contained same score at snapshot time):
```
1587CCF8, 1587CCFC, 5577EA38, 5DCFD0C8, 66B88EF4, 66B88F28, 7F402044
```
`0x68D38E50` (our struct) vs `0x5DCFD0C8` (CE score) are in entirely different
heap regions → they are NOT the same struct. Our RVA_RESULT_STATE offset may
be wrong, OR it points to a different (later-populated) struct.

## Next Steps (Priority Order)

### Step 1: Find the correct struct via CE "Find what writes"
While in a song playing (not on result screen yet), in Cheat Engine:
- Add `5DCFD0C8` to the address list
- Right-click → "Find out what writes to this address"
- Play to completion and get to the result screen
- The instruction list will show exactly which code writes the score
- Note the instruction address (e.g. `sv6c.exe+0x3ABCDE`)

That function or its caller is the correct hook point. Cross-reference that
RVA in Ghidra to understand the surrounding struct layout.

### Step 2: Scan backwards from 5DCFD0C8 in CE
While on the result screen, in CE browse memory from `5DCFD080` (0x48 bytes
before the score). Look for:
- music_id (small integer, 1–9999)
- difficulty (0=NOV, 1=ADV, 2=EXH, 3=MXM)
- score (the value you just played — known)
- ex_score (smaller than score, similar magnitude)
- judgement counts: s-crit (large), crit (medium), near (small), error (very small)
- clear type: 0=fail, 1=played, 2=clear, 3=hard-clear, 4=UC, 5=PUC

The fields likely appear in roughly that order within a 0x40–0x80 byte struct.

### Step 3: Find global pointer to the CE struct
In CE, use "Pointer scan" on `5DCFD0C8` to find static (module-relative)
pointers to it. The result will be chains like `sv6c.exe+0xXXXXXX → offset → 5DCFD0C8`.
This gives us the correct RVA to replace RVA_RESULT_STATE.

### Step 4: Fix the hook timing
Option A: If CE's "what writes" shows a function we can hook AFTER the score
is written, hook that instead of FUN_14029e620.

Option B: Hook the state machine dispatch (RVA_STATE_DISPATCH = 0x4031F0) and
watch for the `konaste_result` state name — the struct should be populated by
the time that state fires.

Option C: In `hk_clear_state`, don't read the struct immediately. Instead, set
a flag, then in a short-polling thread (or on the next `hk_clear_state` call),
keep re-reading the struct until it's non-zero.

## AP Protocol
- WebSocket to `archipelago.gg:PORT`
- Connect packet: `[{"cmd":"Connect","game":"Sound Voltex","name":"SLOT","uuid":"...","version":{"major":0,"minor":5,"build":0,"class":"Version"},"items_handling":7,"tags":[],"slot_data":true}]`
- Location ID: `base_id + music_id * 10 + difficulty`
- Item ID: same formula
- Default base_id: 8000000 (configurable in archipelago.ini)

## archipelago.ini Format
```ini
[archipelago]
host     = archipelago.gg
port     = 38281
slot     = YourSlotName
password =
game     = Sound Voltex
debug_log = 1
log_path  = sdvx_ap.log

[game]
location_base_id = 8000000
item_base_id     = 8000000
goal_clears      = 30
```

## Ghidra MCP Setup (on this machine / any machine)
The Ghidra MCP bridge runs as a Claude MCP server:
```json
"ghidra": {
  "type": "stdio",
  "command": "C:\\Program Files\\Python314\\python.exe",
  "args": ["C:\\Ghidra\\ghidra_12.0.4_PUBLIC\\bridge_mcp_ghidra.py"],
  "env": { "GHIDRA_MCP_URL": "http://127.0.0.1:8089" }
}
```
The `GHIDRA_MCP_URL` env var is required on Windows (Python 3.14 doesn't have
`socket.AF_UNIX`). The Ghidra HTTP extension must be running in Ghidra.
Add this to `~/.claude.json` under `mcpServers` for the project directory.

## TODO (future)
- Song unlock: write to game's unlock flag table when AP item received.
  Requires finding the unlock flag array via RE (not yet done).
- Archipelago world (Python): server-side `worlds/sdvx/` package defining
  items, locations, rules. No existing SDVX world in AP repo.
- `hk_result_upload` (FUN_1403ff640) only fires in e-amusement online mode,
  not local play — can't rely on it for clear detection.
