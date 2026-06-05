"""
Sound Voltex Archipelago World

Items  : 9 input-unlock items (BT-A/B/C/D, FX-L/R, START, Knob LEFT, Knob RIGHT).
         20 level-folder unlock items (LEVEL 1 … LEVEL 20) — each one reveals
         the matching LEVEL difficulty-range folder in the song-select screen.
         5 clear type items (Clear, Hard Clear, Maxxive Clear, UC, PUC) — filler
         used to count progress toward the goal.
         1 Goal Song item (progression, placed only in goal_songs mode).
Locations: Every charted difficulty for every song in music_db (6 302 in the
           bundled dataset; re-run tools/generate_song_data.py when new songs
           are added to the game).
Goal   : song_clears — accumulate goal_clears clear-type items.
         goal_songs  — collect goal_song_count Goal Song items.

The companion DLL (version.dll in the game's modules folder) physically locks
buttons/knobs and hides LEVEL folders until the matching AP items are received.
Song clears are detected via an avs2-core hook and send location checks to the
server.

Item ID layout (ITEM_BASE_ID = 8000000):
  +0  .. +8   : BT-A/B/C/D, FX-L/R, START, Knob LEFT, Knob RIGHT
  +20         : Clear
  +21         : Hard Clear
  +22         : Maxxive Clear
  +23         : UC
  +24         : PUC
  +50         : Goal Song  (progression, goal_songs mode only)
  +101 .. +120: LEVEL 1 … LEVEL 20  (one per difficulty-range folder)
"""

from dataclasses import dataclass
from typing import Dict, List

from BaseClasses import Item, ItemClassification, Location, Region
from worlds.AutoWorld import World, WebWorld
from worlds.generic.Rules import set_rule
from Options import PerGameCommonOptions, Range, Choice

from .data import LOCATION_TABLE, LOCATION_LEVEL, ITEM_BASE_ID


# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

class GoalMode(Choice):
    """Win condition: collect clear-type items (song_clears) or Goal Song items (goal_songs)."""
    display_name = "Goal Mode"
    option_song_clears = 0
    option_goal_songs  = 1
    default = 0


class GoalClears(Range):
    """Number of clear-type items to collect to finish the game (song_clears mode)."""
    display_name = "Goal Clears"
    range_start  = 1
    range_end    = 500
    default      = 30


class GoalSongCount(Range):
    """Number of Goal Song items to collect to finish the game (goal_songs mode)."""
    display_name = "Goal Song Count"
    range_start  = 1
    range_end    = 20
    default      = 5


@dataclass
class SDVXOptions(PerGameCommonOptions):
    goal_mode:       GoalMode
    goal_song_count: GoalSongCount
    goal_clears:     GoalClears


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

# 5 clear type items — filler used to count progress toward the song_clears goal.
# Ordered from easiest to hardest to match the sv6_save_m clear_type values.
CLEAR_ITEMS: List[str] = [
    "Clear",          # +20  8000020  (clear_type 2: Effective Rate)
    "Hard Clear",     # +21  8000021  (clear_type 3: Excessive Rate)
    "Maxxive Clear",  # +22  8000022  (clear_type 6)
    "UC",             # +23  8000023  (clear_type 4)
    "PUC",            # +24  8000024  (clear_type 5)
]
FILLER_ITEM = CLEAR_ITEMS[0]  # "Clear" — used to pad the item pool

GOAL_SONG_ITEM = "Goal Song"  # +50  8000050

# Level-folder unlock items: LEVEL 1 … LEVEL 20.
# Each reveals the corresponding LEVEL difficulty-range folder in song-select.
# The DLL hides all LEVEL folders by default and reveals them as these items
# are received (rel offset 101–120 from ITEM_BASE_ID).
LEVEL_ITEMS: List[str] = [f"LEVEL {n}" for n in range(1, 21)]

# Build item name→ID mapping:
#   input items        : ITEM_BASE_ID + 0..8     (8000000–8000008)
#   clear type items   : ITEM_BASE_ID + 20..24   (8000020–8000024)
#   Goal Song          : ITEM_BASE_ID + 50        (8000050)
#   LEVEL 1 .. LEVEL 20: ITEM_BASE_ID + 101..120  (8000101–8000120)
_ITEM_NAME_TO_ID: Dict[str, int] = {
    name: ITEM_BASE_ID + i for i, name in enumerate(INPUT_ITEMS)
}
for _i, _name in enumerate(CLEAR_ITEMS):
    _ITEM_NAME_TO_ID[_name] = ITEM_BASE_ID + 20 + _i
_ITEM_NAME_TO_ID[GOAL_SONG_ITEM] = ITEM_BASE_ID + 50
for _i, _name in enumerate(LEVEL_ITEMS):
    _ITEM_NAME_TO_ID[_name] = ITEM_BASE_ID + 101 + _i


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
            classification = ItemClassification.progression
        elif name in LEVEL_ITEMS:
            # Level-folder unlock — reveals a difficulty range in song-select.
            classification = ItemClassification.progression
        elif name in CLEAR_ITEMS:
            # progression_skip_balancing: tracked by state.count() for the goal
            # condition but not sphere-balanced, which keeps fill fast.
            classification = ItemClassification.progression_skip_balancing
        elif name == GOAL_SONG_ITEM:
            classification = ItemClassification.progression
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

        # In goal_songs mode, add the required Goal Song progression items
        if self.options.goal_mode == GoalMode.option_goal_songs:
            for _ in range(self.options.goal_song_count.value):
                items.append(self.create_item(GOAL_SONG_ITEM))

        # Pad the rest of the pool with Clear items (one per remaining slot)
        num_locations = len(self.location_name_to_id)
        while len(items) < num_locations:
            items.append(self.create_item(FILLER_ITEM))

        self.multiworld.itempool += items

    # -----------------------------------------------------------------------
    # Rules
    # -----------------------------------------------------------------------

    def set_rules(self) -> None:
        for location in self.multiworld.get_locations(self.player):
            level = LOCATION_LEVEL.get(location.name)

            def make_rule(lv: int | None):
                def rule(state) -> bool:
                    return (
                        state.has("START",  self.player) and
                        state.has("BT-A",   self.player) and
                        state.has("BT-B",   self.player) and
                        state.has("BT-C",   self.player) and
                        state.has("BT-D",   self.player) and
                        (lv is None or state.has(f"LEVEL {lv}", self.player))
                    )
                return rule

            set_rule(location, make_rule(level))

    # -----------------------------------------------------------------------
    # Completion condition
    # -----------------------------------------------------------------------

    def generate_basic(self) -> None:
        if self.options.goal_mode == GoalMode.option_goal_songs:
            count = self.options.goal_song_count.value
            self.multiworld.completion_condition[self.player] = (
                lambda state, c=count: state.has(GOAL_SONG_ITEM, self.player, c)
            )
        else:
            goal = self.options.goal_clears.value
            self.multiworld.completion_condition[self.player] = (
                lambda state, g=goal: sum(
                    state.count(n, self.player) for n in CLEAR_ITEMS
                ) >= g
            )

    # -----------------------------------------------------------------------
    # Slot data
    # -----------------------------------------------------------------------

    def fill_slot_data(self) -> dict:
        return {
            "goal_mode":       self.options.goal_mode.value,
            "goal_clears":     self.options.goal_clears.value,
            "goal_song_count": self.options.goal_song_count.value,
        }
