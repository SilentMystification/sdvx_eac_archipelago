# SDVX EAC Archipelago

A DLL mod for **Sound Voltex EXCEED GEAR (sv6c)** that connects the game to an [Archipelago](https://archipelago.gg) multiworld session. Buttons and knobs start locked and are unlocked as Archipelago items are received. Song clears send location checks to the server.

---

## Requirements

- **Visual Studio 2022 Build Tools** with the MSVC v143 toolset
- **Windows SDK 10.0.26100.0**
- Sound Voltex EXCEED GEAR (sv6c) with bemanitools / dinputhook-sdvx

---

## Building

### 1. Check toolchain paths

Open `build.bat` and verify the following lines match your installation:

```bat
set "MSVC_VER=14.44.35207"
set "VS_BASE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "WINSDK_VER=10.0.26100.0"
set "WINSDK_BASE=C:\Program Files (x86)\Windows Kits\10"
```

If your VS Build Tools or SDK are installed elsewhere, update these four lines accordingly. The MSVC version number (`14.44.35207`) can be found by browsing to:

```
<VS_BASE>\VC\Tools\MSVC\
```

The folder name inside is your version number.

### 2. Build

```bat
build.bat
```

Output: `build\version.dll`

---

## Deploying

### 1. Check the deploy path

Open `build_deploy.bat` and verify this line points to your game's `modules` folder:

```bat
set "DEPLOY_DIR=C:\Games\SOUND VOLTEX EXCEED GEAR\game\modules"
```

Update it if your game is installed elsewhere.

### 2. Build and deploy in one step

```bat
build_deploy.bat
```

This builds the DLL and copies it directly to the game folder.

---

## Configuration

Copy `archipelago.ini.example` into the same `game\modules\` folder and rename it to `archipelago.ini`, then edit it:

```ini
[Archipelago]
host     = archipelago.gg   ; server hostname or IP
port     = 38281
slot     = YourName          ; must match your slot name in the AP room
password =                   ; leave blank if the room has no password

[Game]
goal_clears = 30             ; number of song clears needed to finish

[Debug]
enabled  = 0                 ; set to 1 to write sdvx_ap.log next to the DLL
```

---

## How it works

- The mod loads as a `version.dll` proxy, forwarding all real version API calls to the system DLL.
- It hooks `sdvx_io_get_input_gpio` and `sdvx_io_get_spinner_pos` in `sdvxio.dll` to gate inputs until unlocked by AP items.
- It hooks `property_destroy` in `avs2-core.dll` to intercept `game.sv6_save_m` e-amusement packets and detect song clears.
- The AP client connects over WebSocket and handles item/location sync in a background thread.

---

## Item layout

| Item | Description |
|------|-------------|
| BT-A, BT-B, BT-C, BT-D | Individual BT buttons |
| FX-L, FX-R | FX buttons |
| START | Start button |
| Knob LEFT, Knob RIGHT | Analog knobs |
| Song unlocks | Individual songs/difficulties (future) |

---

## Credits

- **[SilentMystification](https://github.com/SilentMystification)** — mod author
- **[Archipelago](https://github.com/ArchipelagoMW/Archipelago)** — the Archipelago team — multiworld randomizer framework
- **[MinHook](https://github.com/tsudakageyu)** — Tsuda Kageyu — minimalistic API hooking library for x64/x86
- **[bemanitools](https://github.com/djhackersdev/bemanitools)** — xyen / djhackers — BEMANI game tooling including the SDVX IO API
- **[kshook](https://github.com/emskye96/kshook)** — emskye96 — SDVX score hook reference implementation
