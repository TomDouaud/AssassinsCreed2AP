"""Item name -> id tables and classification for AC2AP."""
from __future__ import annotations

from typing import Dict, NamedTuple

from BaseClasses import ItemClassification

# Same reserved id range as Locations.py, offset so location and item ids never collide.
BASE_ID = 20240002000
ITEM_ID_OFFSET = 10000


class AC2ItemData(NamedTuple):
    id: int
    classification: ItemClassification
    count: int = 1


PROGRESSIVE_SEQUENCE = "Progressive Sequence"
CODEX_PAGE = "Codex Page"
ASSASSIN_SEAL = "Assassin Seal"
PROGRESSIVE_HIDDEN_BLADE = "Progressive Hidden Blade"

FLORINS_SMALL = "100 Florins"
FLORINS_MEDIUM = "500 Florins"
FLORINS_LARGE = "1000 Florins"

# Traps - ids aligned with the C++ client (dllmain.cpp item_ids)
TRAP_TEMPLAR_TAX = "Templar Tax Trap"
TRAP_BAD_MEDICINE = "Bad Medicine Trap"
TRAP_DEATH = "Death Trap"
TRAP_WANTED = "Wanted Trap"

# Sequence 1 is the starting state, so 13 progressive items unlock Sequences 2-14.
PROGRESSIVE_SEQUENCE_COUNT = 13
CODEX_PAGE_COUNT = 30
ASSASSIN_SEAL_COUNT = 6
PROGRESSIVE_HIDDEN_BLADE_COUNT = 4

ITEM_TABLE: Dict[str, AC2ItemData] = {
    PROGRESSIVE_SEQUENCE: AC2ItemData(
        BASE_ID + ITEM_ID_OFFSET + 0, ItemClassification.progression, PROGRESSIVE_SEQUENCE_COUNT
    ),
    CODEX_PAGE: AC2ItemData(
        BASE_ID + ITEM_ID_OFFSET + 1, ItemClassification.progression, CODEX_PAGE_COUNT
    ),
    ASSASSIN_SEAL: AC2ItemData(
        BASE_ID + ITEM_ID_OFFSET + 2, ItemClassification.useful, ASSASSIN_SEAL_COUNT
    ),
    PROGRESSIVE_HIDDEN_BLADE: AC2ItemData(
        BASE_ID + ITEM_ID_OFFSET + 3, ItemClassification.useful, PROGRESSIVE_HIDDEN_BLADE_COUNT
    ),
    FLORINS_SMALL: AC2ItemData(BASE_ID + ITEM_ID_OFFSET + 4, ItemClassification.filler),
    FLORINS_MEDIUM: AC2ItemData(BASE_ID + ITEM_ID_OFFSET + 5, ItemClassification.filler),
    FLORINS_LARGE: AC2ItemData(BASE_ID + ITEM_ID_OFFSET + 6, ItemClassification.filler),
    # Traps: ids +100.. aligned with the client (item_ids::TRAP_* in dllmain.cpp)
    TRAP_TEMPLAR_TAX: AC2ItemData(BASE_ID + ITEM_ID_OFFSET + 100, ItemClassification.trap),
    TRAP_BAD_MEDICINE: AC2ItemData(BASE_ID + ITEM_ID_OFFSET + 101, ItemClassification.trap),
    TRAP_DEATH: AC2ItemData(BASE_ID + ITEM_ID_OFFSET + 102, ItemClassification.trap),
    TRAP_WANTED: AC2ItemData(BASE_ID + ITEM_ID_OFFSET + 103, ItemClassification.trap),
}

# Death Trap removed from the pool: redundant with DeathLink (user decision).
# The definition and id stay reserved (the client keeps kill_player for DeathLink).
TRAP_ITEMS = (TRAP_TEMPLAR_TAX, TRAP_BAD_MEDICINE, TRAP_WANTED)

# --- Equipment items (counterpart of the shop locations) ---------------------
# Each shop object circulates in the multiworld: a player in another world can
# receive "Old Syrian Sword", and you can receive it from them.
# Created ONLY when the shop_items option is active (see create_items), to keep
# item_count == location_count. The names are EXACTLY those of the
# "Shop - <name>" locations (Locations.py) without the prefix.
# ids: sub-range +200.. (aligned later on the client side for the RAM grant).
from .Locations import WEAPONS, ARMOR_PIECES, DYES, PAINTINGS, POUCHES

SHOP_ITEM_BASE = BASE_ID + ITEM_ID_OFFSET + 200
# Weapons/armor/pouches = useful (gameplay impact); dyes/paintings = filler (cosmetic).
_shop_useful = WEAPONS + ARMOR_PIECES + POUCHES
_shop_filler = DYES + PAINTINGS
SHOP_ITEM_NAMES = _shop_useful + _shop_filler
for _i, _name in enumerate(SHOP_ITEM_NAMES):
    _cls = ItemClassification.useful if _name in _shop_useful else ItemClassification.filler
    ITEM_TABLE[_name] = AC2ItemData(SHOP_ITEM_BASE + _i, _cls)

ITEM_NAME_TO_ID: Dict[str, int] = {name: data.id for name, data in ITEM_TABLE.items()}

# Items placed once per their declared count, everything else (florin packs) is filler
# used to pad the pool up to the location count. See create_items in __init__.py.
PROGRESSION_ITEMS = (PROGRESSIVE_SEQUENCE, CODEX_PAGE)
USEFUL_ITEMS = (ASSASSIN_SEAL, PROGRESSIVE_HIDDEN_BLADE)
FILLER_ITEMS = (FLORINS_SMALL, FLORINS_MEDIUM, FLORINS_LARGE)
