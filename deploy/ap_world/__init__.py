"""
Sound Voltex Archipelago World

Items  : 9 input-unlock items (BT-A/B/C/D, FX-L/R, START, Knob LEFT, Knob RIGHT).
         20 level-folder unlock items (LEVEL 1 … LEVEL 20) — each one reveals
         the matching LEVEL difficulty-range folder in the song-select screen.
         The rest of the item pool is filled with "Song Clear" filler items.
Locations: Every charted difficulty for every song in music_db (6 302 in the
           bundled dataset; re-run tools/generate_song_data.py when new songs
           are added to the game).
Goal   : Accumulate <goal_clears> "Song Clear" items.

The companion DLL (version.dll in the game's modules folder) physically locks
buttons/knobs and hides LEVEL folders until the matching AP items are received.
Song clears are detected via an avs2-core hook and send location checks to the
server.

Item ID layout (ITEM_BASE_ID = 8000000):
  +0  .. +8   : BT-A/B/C/D, FX-L/R, START, Knob LEFT, Knob RIGHT
  +9          : Song Clear  (filler)
  +10 .. +29  : LEVEL 1 … LEVEL 20  (one per difficulty-range folder)
"""

from dataclasses import dataclass
from typing import Any, Dict, List

from BaseClasses import Item, ItemClassification, Location, Region
from worlds.AutoWorld import World, WebWorld
from Options import PerGameCommonOptions, Range

from .data import SONGS, LOCATION_TABLE, LOCATION_BASE_ID, ITEM_BASE_ID


# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

class GoalClears(Range):
    """Number of 'Song Clear' items to collect to finish the game."""
    display_name = "Goal Clears"
    range_start  = 1
    range_end    = 500
    default      = 30


@dataclass
class SDVXOptions(PerGameCommonOptions):
    goal_clears: GoalClears


# ---------------------------------------------------------------------------
# Items
# ---------------------------------------------------------------------------

# The 9 physical-input unlock items, in the order they are assigned IDs.
INPUT_ITEMS: List[str] = [
    "BT-A", "BT-B", "BT-C", "BT-D",
    "FX-L", "FX-R",
    "START",
    "Knob LEFT", "Knob RIGHT",
]
FILLER_ITEM = "Song Clear"

# Level-folder unlock items: LEVEL 1 … LEVEL 20.
# Each reveals the corresponding LEVEL difficulty-range folder in song-select.
# The DLL hides all LEVEL folders by default and reveals them as these items
# are received (rel offset 10–29 from ITEM_BASE_ID).
LEVEL_ITEMS: List[str] = [f"LEVEL {n}" for n in range(1, 21)]

# Build item name→ID mapping:
#   input items        : ITEM_BASE_ID + 0..8   (8000000–8000008)
#   Song Clear         : ITEM_BASE_ID + 9       (8000009)
#   LEVEL 1 .. LEVEL 20: ITEM_BASE_ID + 10..29  (8000010–8000029)
#   (additional items can use ITEM_BASE_ID + 30 and above)
# Locations start at 8100000 (LOCATION_BASE_ID), so there is no overlap.
_ITEM_NAME_TO_ID: Dict[str, int] = {
    name: ITEM_BASE_ID + i for i, name in enumerate(INPUT_ITEMS)
}
_ITEM_NAME_TO_ID[FILLER_ITEM] = ITEM_BASE_ID + len(INPUT_ITEMS)  # +9
for _i, _name in enumerate(LEVEL_ITEMS):
    _ITEM_NAME_TO_ID[_name] = ITEM_BASE_ID + len(INPUT_ITEMS) + 1 + _i  # +10..+29


class SDVXItem(Item):
    game = "Sound Voltex"


# ---------------------------------------------------------------------------
# Locations
# ---------------------------------------------------------------------------

class SDVXLocation(Location):
    game = "Sound Voltex"


# ---------------------------------------------------------------------------
# Web settings (shown in the AP options page)
# ---------------------------------------------------------------------------

class SDVXWebWorld(WebWorld):
    theme = "ocean"
    tutorials = []   # TODO: add a setup guide


# ---------------------------------------------------------------------------
# World
# ---------------------------------------------------------------------------

class SDVXWorld(World):
    """
    Sound Voltex Exceed Gear — buttons and knobs are locked until unlocked by
    received Archipelago items. Song clears send location checks.
    """

    game              = "Sound Voltex"
    web               = SDVXWebWorld()
    options_dataclass = SDVXOptions
    options: SDVXOptions

    item_name_to_id     = _ITEM_NAME_TO_ID
    location_name_to_id = LOCATION_TABLE   # imported from data.py

    # -----------------------------------------------------------------------
    # Region / location setup
    # -----------------------------------------------------------------------

    def create_regions(self) -> None:
        menu = Region("Menu", self.player, self.multiworld)
        self.multiworld.regions.append(menu)

        for loc_name, loc_id in self.location_name_to_id.items():
            loc = SDVXLocation(self.player, loc_name, loc_id, menu)
            menu.locations.append(loc)

    # -----------------------------------------------------------------------
    # Items
    # -----------------------------------------------------------------------

    def create_item(self, name: str) -> SDVXItem:
        """Create a single item by name.  Called by AP internals as well as
        our own create_items (start inventory, item links, plando, etc.)."""
        if name in INPUT_ITEMS:
            # Physical button/knob unlock — hard progression gating.
            classification = ItemClassification.progression
        elif name in LEVEL_ITEMS:
            # Level-folder unlock — reveals a difficulty range in song-select.
            # Marked progression so the fill algorithm places them accessibly.
            classification = ItemClassification.progression
        elif name == FILLER_ITEM:
            # progression_skip_balancing: tracked by state.has() for the goal
            # condition but not sphere-balanced, which keeps fill fast.
            classification = ItemClassification.progression_skip_balancing
        else:
            raise KeyError(f"Unknown Sound Voltex item: {name!r}")
        return SDVXItem(name, classification, self.item_name_to_id[name], self.player)

    def get_filler_item_name(self) -> str:
        """Return the name of the filler item so AP never accidentally uses a
        progression input-unlock as a spontaneous filler replacement."""
        return FILLER_ITEM

    def create_items(self) -> None:
        # 9 physical-input unlock items (BT-A/B/C/D, FX-L/R, START, knobs)
        items: List[SDVXItem] = [self.create_item(name) for name in INPUT_ITEMS]

        # 20 level-folder unlock items (LEVEL 1 … LEVEL 20)
        items += [self.create_item(name) for name in LEVEL_ITEMS]

        # Pad the rest of the pool with Song Clear items (one per remaining slot)
        num_locations = len(self.location_name_to_id)
        while len(items) < num_locations:
            items.append(self.create_item(FILLER_ITEM))

        self.multiworld.itempool += items

    # -----------------------------------------------------------------------
    # Rules
    # -----------------------------------------------------------------------

    def set_rules(self) -> None:
        # All song-clear locations are logically reachable from the start —
        # the DLL enforces the physical gating in-game.
        # TODO: add logic (e.g. require START to enter a credit) once we have
        #       enough playtesting data to know what minimum is required.
        pass

    # -----------------------------------------------------------------------
    # Completion condition
    # -----------------------------------------------------------------------

    def generate_basic(self) -> None:
        goal = self.options.goal_clears.value
        self.multiworld.completion_condition[self.player] = (
            lambda state, g=goal: state.has(FILLER_ITEM, self.player, g)
        )
