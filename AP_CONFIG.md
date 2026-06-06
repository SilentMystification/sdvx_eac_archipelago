# SDVX AP Configuration Reference

## archipelago.ini

`archipelago.ini` lives in the game's `modules\` folder alongside the DLL (copied there from `deploy\`). Edit it before launching the game.

### [Archipelago]

| Key | Description | Default |
|-----|-------------|---------|
| `host` | Archipelago server hostname or IP | `archipelago.gg` |
| `port` | Archipelago server port | `38281` |
| `slot` | Your player slot name — must match exactly what was used when generating the seed | *(required)* |
| `password` | Room password — leave blank if none | *(blank)* |
| `game` | Game name — must match the AP world name exactly | `Sound Voltex` |

### [Game]

| Key | Description | Default |
|-----|-------------|---------|
| `item_base_id` | Base item ID — must match the AP world package | `8000000` |
| `location_base_id` | Base location ID — must match the AP world package | `8100000` |
| `goal_mode` | Win condition mode (see [Goal modes](#goal-modes) below) | `0` |
| `goal_clears` | Clear-type items required to win in `song_clears` mode | `30` |
| `goal_song_count` | Goal Song items required to win in `goal_songs` mode | `5` |
| `goal_level` | Minimum level folder Goal Song items are placed behind in `goal_songs` mode; `0` = no restriction | `0` |

> **Note:** `goal_mode`, `goal_clears`, `goal_song_count`, and `goal_level` are overridden by slot data received from the AP server on connect. The ini values serve as the default before connection is established — keep them in sync with your YAML to avoid a mismatch window.

### [Debug]

| Key | Description | Default |
|-----|-------------|---------|
| `enabled` | Set to `1` to write a log file next to the DLL | `0` |
| `log_path` | Log file name (relative to the DLL) | `sdvx_ap.log` |

---

## Goal modes

### Mode 0 — `song_clears`

Clear songs to accumulate clear-type items. The grade you get on each clear determines which item type is sent. Goal fires when your total count reaches `goal_clears`.

| Clear grade | Item received |
|-------------|--------------|
| Effective Rate (clear) | Clear |
| Excessive Rate (hard clear) | Hard Clear |
| Maxxive Rate | Maxxive Clear |
| Ultimate Chain | UC |
| Perfect Ultimate Chain | PUC |

**Relevant options:** `goal_clears`

### Mode 1 — `goal_songs`

Collect `goal_song_count` Goal Song progression items. These are placed by the AP fill algorithm. By default they can appear anywhere in the multiworld. Set `goal_level` to restrict them to locations that require a minimum level folder to be unlocked — forcing high-level play before the goal is reachable.

**Relevant options:** `goal_song_count`, `goal_level`

| `goal_level` | Behaviour |
|---|---|
| `0` | Goal Song items placed anywhere (unrestricted) |
| `1`–`20` | Goal Song items only placed at locations requiring at least that LEVEL folder |

---

## Item and location IDs

IDs are baked into seeds — existing ranges must not shift once seeds are in the wild.

### Items — `ITEM_BASE_ID = 8000000`

| Offset | Item | Notes |
|--------|------|-------|
| +0 | BT-A | Input unlock |
| +1 | BT-B | Input unlock |
| +2 | BT-C | Input unlock |
| +3 | BT-D | Input unlock |
| +4 | FX-L | Input unlock |
| +5 | FX-R | Input unlock |
| +6 | START | Input unlock |
| +7 | Knob LEFT | Input unlock |
| +8 | Knob RIGHT | Input unlock |
| +9 … +19 | *(unused)* | |
| +20 | Clear | Clear-type filler (`clear_type` 2 — Effective Rate) |
| +21 | Hard Clear | Clear-type filler (`clear_type` 3 — Excessive Rate) |
| +22 | Maxxive Clear | Clear-type filler (`clear_type` 6) |
| +23 | UC | Clear-type filler (`clear_type` 4) |
| +24 | PUC | Clear-type filler (`clear_type` 5) |
| +25 … +49 | *(unused)* | |
| +50 | Goal Song | Progression item — placed in `goal_songs` mode |
| +51 … +100 | *(unused)* | |
| +101 | LEVEL 1 | Level-folder unlock |
| +102 | LEVEL 2 | Level-folder unlock |
| … | … | |
| +120 | LEVEL 20 | Level-folder unlock |

### Locations — `LOCATION_BASE_ID = 8100000`

```
location_id = LOCATION_BASE_ID + music_id * 10 + difficulty
```

| `difficulty` value | Difficulty name |
|---|---|
| 0 | NOVICE |
| 1 | ADVANCED |
| 2 | EXHAUST |
| 3 | MAXIMUM / INFINITE / GRAVITY / etc. |

Examples using real songs (`music_id` values from `music_db.xml`):

| Location | Formula | ID |
|---|---|---|
| ALBIDA Powerless Mix — NOVICE | 8100000 + 1×10 + 0 | 8100010 |
| ALBIDA Powerless Mix — INFINITE | 8100000 + 1×10 + 3 | 8100013 |
| Broken 8cmix — EXHAUST | 8100000 + 2×10 + 2 | 8100022 |
| PULSE LASER — ADVANCED | 8100000 + 6×10 + 1 | 8100061 |
