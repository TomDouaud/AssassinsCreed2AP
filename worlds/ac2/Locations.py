"""Location name -> id tables for AC2AP."""
from __future__ import annotations

from typing import Dict, NamedTuple, Optional

# Reserved id range for this world. Never change once released (AP id stability contract).
BASE_ID = 20240002000

# --- ID allocation plan ---------------------------------------------------
# Each category gets its own 1000-wide sub-range off BASE_ID so ids stay stable
# and there is headroom to grow any single category without colliding with the
# next one. Counts below are the number actually allocated today; the unused
# tail of each sub-range is reserved, not reused by another category.
#
#   BASE_ID +    0 ..  999   Main Missions       (86 used)   - pre-existing, unchanged
#   BASE_ID + 1000 .. 1999   Viewpoints          (73 used)
#   BASE_ID + 2000 .. 2999   Feathers            (100 used)
#   BASE_ID + 3000 .. 3999   Glyphs              (20 used)
#   BASE_ID + 4000 .. 4999   Secondary Missions  (47 used: 30 contracts + 6 races + 6 couriers + 5 beat-ups)
#   BASE_ID + 5000 .. 5999   Tombs               (6 used)
#   BASE_ID + 6000 .. 6999   Shop Items          (89 used: 22 weapons + 16 armor + 14 dyes + 30 paintings + 7 pouches)
#   BASE_ID + 7000 .. 7999   Villa Renovations   (10 used)
#   BASE_ID + 9000 .. 9999   Treasure Chests     (330 used)
# Items.py uses BASE_ID + ITEM_ID_OFFSET (10000+) so locations and items never collide.
VIEWPOINTS_BASE = BASE_ID + 1000
FEATHERS_BASE = BASE_ID + 2000
GLYPHS_BASE = BASE_ID + 3000
SECONDARY_BASE = BASE_ID + 4000
TOMBS_BASE = BASE_ID + 5000
SHOP_BASE = BASE_ID + 6000
VILLA_BASE = BASE_ID + 7000
STATUES_BASE = BASE_ID + 8200  # 8 Monteriggioni statuettes (after the 8000/8100 districts)
CODEX_BASE = BASE_ID + 8300    # 30 codex pages (picked up from codex targets/chests)
CHESTS_BASE = BASE_ID + 9000


class AC2LocationData(NamedTuple):
    id: int
    region: str


# Sequence -> (region, [mission names in order]).
# Names are the exact mission titles from docs/02-checks-database.md, prefixed with
# "Sequence <n> - " to build the location name. Sequence 12/13 are the bundled DLCs.
MAIN_MISSIONS_BY_SEQUENCE: Dict[int, tuple[str, list[str]]] = {
    1: ("Florence", [
        "Boys Will Be Boys", "You Should See the Other Guy", "Sibling Rivalry", "Nightcap",
        "Paperboy", "Beat a Cheat", "Petruccio's Secret", "Friend of the Family",
        "Special Delivery", "Jailbird", "Family Heirloom", "Last Man Standing",
    ]),
    2: ("Florence", [
        "Fitting In", "Ace Up My Sleeve", "Judge, Jury, Executioner", "Laying Low", "Arrivederci",
    ]),
    3: ("Florence", [
        "Roadside Assistance", "Casa Dolce Casa", "Practice Makes Perfect", "What Goes Around",
        "A Change of Plans",
    ]),
    4: ("Florence", [
        "Practice What You Preach", "Fox Hunt", "See You There", "Novella's Secret",
        "Wolves in Sheep's Clothing", "Farewell Francesco",
    ]),
    5: ("Tuscany", [
        "Four to the Floor", "Evasive Maneuvers", "Town Crier", "Come Out and Play",
        "The Cowl Does Not Make the Monk", "Behind Closed Doors", "With Friends Like These",
    ]),
    6: ("Tuscany", [
        "Road Trip", "Romagna Holiday", "Tutti a Bordo",
    ]),
    7: ("Venice", [
        "Benvenuto", "That's Gonna Leave a Mark", "Building Blocks", "Breakout",
        "Clothes Make the Man", "Cleaning House", "Monkey See, Monkey Do", "By Leaps and Bounds",
        "Everything Must Go",
    ]),
    8: ("Venice", [
        "Birds of a Feather", "If at First You Don't Succeed...", "Nothing Ventured, Nothing Gained",
        "Well Begun Is Half Done", "Infrequent Flier",
    ]),
    9: ("Venice", [
        "Knowledge Is Power", "Damsel in Distress", "Nun the Wiser", "Capture the Flag",
        "And They're Off", "Cheaters Never Prosper", "Having a Blast",
    ]),
    10: ("Venice", [
        "An Unpleasant Turn of Events", "Caged Fighter", "Leave No Man Behind",
        "Assume the Position", "Two Birds, One Blade",
    ]),
    11: ("Venice", [
        "All Things Come to He Who Waits", "Play Along",
    ]),
    12: ("Forli", [
        "A Warm Welcome", "Bodyguard", "Holding the Fort", "Godfather", "Checcomate",
        "Far From the Tree",
    ]),
    13: ("Florence", [
        "Florentine Fiasco", "Still Life", "Doomsday", "Arch Nemesis", "Port Authority",
        "Upward Mobility", "Last Rites", "Hitting the Hay", "Climbing the Ranks",
        "Surgical Strike", "Power to the People", "Mob Justice",
    ]),
    14: ("Rome", [
        "X Marks the Spot", "In Bocca al Lupo",
    ]),
}


def _build_main_mission_locations() -> Dict[str, AC2LocationData]:
    locations: Dict[str, AC2LocationData] = {}
    next_id = BASE_ID
    for sequence in sorted(MAIN_MISSIONS_BY_SEQUENCE):
        region, missions = MAIN_MISSIONS_BY_SEQUENCE[sequence]
        for mission in missions:
            name = f"Sequence {sequence} - {mission}"
            locations[name] = AC2LocationData(next_id, region)
            next_id += 1
    return locations


MAIN_MISSION_LOCATIONS: Dict[str, AC2LocationData] = _build_main_mission_locations()

# Bonfire of the Vanities district liberations (2). Story-mandatory DLC arc events with
# their own persistent mission records (live-confirmed 2026-07-12; forge:
# SQ13_SantaMariaNovella_liberate / SQ13_SanGiovanni_liberate). Dedicated id sub-range so
# the positional main-mission ids stay stable. The "Sequence 13 - " prefix makes them
# sequence-gated like any other Sequence 13 location.
LIBERATIONS_BASE = BASE_ID + 8400
MAIN_MISSION_LOCATIONS["Sequence 13 - Liberate Santa Maria Novella"] = \
    AC2LocationData(LIBERATIONS_BASE + 0, "Florence")
MAIN_MISSION_LOCATIONS["Sequence 13 - Liberate San Giovanni"] = \
    AC2LocationData(LIBERATIONS_BASE + 1, "Florence")

# --- Viewpoints (73) -------------------------------------------------------
# "Par compte" design (docs/re/correlation-02-07.md): detection is per-account,
# no reliable per-viewpoint place name exists yet. Numbered locations, evenly
# spread across the regions that have viewpoints in vanilla (Florence, Tuscany,
# Forli, Venice - Monteriggioni/Rome have none per docs/03). Distribution is a
# simple round-robin, not a claim about exact vanilla placement.
VIEWPOINT_REGIONS = ("Florence", "Tuscany", "Forli", "Venice")
VIEWPOINT_COUNT = 73


def _build_viewpoint_locations() -> Dict[str, AC2LocationData]:
    locations: Dict[str, AC2LocationData] = {}
    for i in range(VIEWPOINT_COUNT):
        region = VIEWPOINT_REGIONS[i % len(VIEWPOINT_REGIONS)]
        locations[f"Viewpoint #{i + 1}"] = AC2LocationData(VIEWPOINTS_BASE + i, region)
    return locations


VIEWPOINT_LOCATIONS: Dict[str, AC2LocationData] = _build_viewpoint_locations()

# --- Feathers (100) ---------------------------------------------------------
# Same "par compte" design as viewpoints. Vanilla spread per docs/02: Florence,
# Monteriggioni, Tuscany, Forli, Venice.
FEATHER_REGIONS = ("Florence", "Monteriggioni", "Tuscany", "Forli", "Venice")
FEATHER_COUNT = 100


def _build_feather_locations() -> Dict[str, AC2LocationData]:
    locations: Dict[str, AC2LocationData] = {}
    for i in range(FEATHER_COUNT):
        region = FEATHER_REGIONS[i % len(FEATHER_REGIONS)]
        locations[f"Feather #{i + 1}"] = AC2LocationData(FEATHERS_BASE + i, region)
    return locations


FEATHER_LOCATIONS: Dict[str, AC2LocationData] = _build_feather_locations()

# --- Glyphs (20) -------------------------------------------------------------
# Glyphs sit on landmarks across every city region (docs/02). Round-robin same
# as viewpoints/feathers - no confirmed 1:1 landmark-to-region map yet.
GLYPH_REGIONS = ("Florence", "Tuscany", "Forli", "Venice")
GLYPH_COUNT = 20


def _build_glyph_locations() -> Dict[str, AC2LocationData]:
    locations: Dict[str, AC2LocationData] = {}
    for i in range(GLYPH_COUNT):
        region = GLYPH_REGIONS[i % len(GLYPH_REGIONS)]
        locations[f"Glyph #{i + 1}"] = AC2LocationData(GLYPHS_BASE + i, region)
    return locations


GLYPH_LOCATIONS: Dict[str, AC2LocationData] = _build_glyph_locations()

# --- Secondary Missions (47) -------------------------------------------------
# Assassination contracts have confirmed names + cities (docs/02). Races,
# courier missions and beat-ups have no confirmed names in the docs (marked
# with warning icons, "to be verified in-game") - numbered placeholders used
# instead of inventing names, attached to Menu (no confirmed region/gate yet).
ASSASSINATION_CONTRACTS: Dict[str, list[str]] = {
    "Florence": [
        "Day at the Market", "Fallen Archers", "Political Suicide", "Caveat Emptor",
        "Meeting Adjourned", "Needle in a Haystack", "Peacekeeper", "Leader of the Pack",
    ],
    "Tuscany": [
        "Reap What You Sow", "Don't Get Your Hands Dirty", "Supply in Demand", "Flee Market",
        "Vertical Slice", "No Camping", "Showtime",
    ],
    "Forli": [
        "Thin the Ranks", "Beginnings of a Conspiracy", "Arch Enemies", "Wet Work",
        "Dead on Arrival", "Go Towards the Light", "Mark and Execute", "Thicker Than Water",
    ],
    "Venice": [
        "Zero Tolerance", "Blade in the Crowd", "Honorable Thief", "No Laughing Matter",
        "Crash a Party", "False Legacy", "Hunting the Hunter",
    ],
}

# Exact race/courier/beat-up names from the forge (docs/re/secondary-ids.txt).
# 5 of each (the forge is authoritative: docs/02 said 6, which was wrong). CMB03/CMB04 have
# no loc name -> descriptive UITempString.
RACES = ["San Gimignano Dash", "Florentine Sprint", "Romagna Hustle",
         "Venetian Rush", "San Marco Scuttle"]
COURIERS = ["Wedding Bells Are Ringing", "The Messenger's Burden", "Speedy Delivery",
            "The Perfect Marriage", "Casanova"]
BEATUPS = ["A Woman Scorned", "Wanton Hubby", "Jealous Suitor",
           "Cheating Husband", "Spear of Infidelity"]


def _build_secondary_mission_locations() -> Dict[str, AC2LocationData]:
    locations: Dict[str, AC2LocationData] = {}
    next_id = SECONDARY_BASE
    for region, contracts in ASSASSINATION_CONTRACTS.items():
        for contract in contracts:
            locations[f"Contract - {contract}"] = AC2LocationData(next_id, region)
            next_id += 1
    for name in RACES:
        locations[f"Race - {name}"] = AC2LocationData(next_id, "Menu")
        next_id += 1
    for name in COURIERS:
        locations[f"Courier - {name}"] = AC2LocationData(next_id, "Menu")
        next_id += 1
    for name in BEATUPS:
        locations[f"Beat-up - {name}"] = AC2LocationData(next_id, "Menu")
        next_id += 1
    return locations


SECONDARY_MISSION_LOCATIONS: Dict[str, AC2LocationData] = _build_secondary_mission_locations()

# --- Tombs (6) ----------------------------------------------------------
# Assassin Tombs, names + cities from docs/02. San Gimignano maps to our
# "Tuscany" region (no separate San Gimignano region in this world).
TOMBS: list[tuple[str, str]] = [
    ("Novella's Secret", "Florence"),
    ("Il Duomo's Secret", "Florence"),
    ("Torre Grossa's Secret", "Tuscany"),
    ("Ravaldino's Secret", "Forli"),
    ("San Marco's Secret", "Venice"),
    ("Visitazione's Secret", "Venice"),
]


def _build_tomb_locations() -> Dict[str, AC2LocationData]:
    locations: Dict[str, AC2LocationData] = {}
    for i, (name, region) in enumerate(TOMBS):
        locations[f"Tomb - {name}"] = AC2LocationData(TOMBS_BASE + i, region)
    return locations


TOMB_LOCATIONS: Dict[str, AC2LocationData] = _build_tomb_locations()

# --- Shop Items (BY PRESENCE, forge names) -----------------------------------
# Rebuilt from the FORGE names. Reconciling internal name <-> wiki shop name has NO reliable
# automatic source (stats absent from EquipmentSettings, no item->String ID link, display names
# absent from the extracted loc). So locations are named after the forge item, order = shop order
# ("Weapon - Sword A" = first sword from the top, etc.).
# Detected by PRESENCE of the forge id in the save (purchase = acquired item). SHOP_EQUIPMENT
# (forge_id, name) is also used by gen_map.py. All in Monteriggioni (single shop once opened).
SHOP_EQUIPMENT: list[tuple[int, str]] = [
    (0x784ECD82, "Weapon - Sword A"),
    (0x784ECD7D, "Weapon - Sword B"),
    (0x784ECD96, "Weapon - Sword C"),
    (0x784ECD91, "Weapon - Sword D"),
    (0x26740BA6, "Weapon - Sword E"),
    (0x784EC41A, "Weapon - Falchion A"),
    (0x784ECDA0, "Weapon - Falchion B"),
    (0x784ECD9B, "Weapon - Mace A"),
    (0x784ECDAA, "Weapon - Mace B"),
    (0x784ECD8C, "Weapon - War Hammer A"),
    (0x784ECDAF, "Weapon - War Hammer B"),
    (0x26740BAB, "Weapon - Saber"),
    (0x784ECDA5, "Weapon - Scimitar"),
    (0x784ECD87, "Weapon - Sledgehammer"),
    (0x784EC41F, "Weapon - Dagger"),
    (0x784EC424, "Weapon - Knife"),
    (0x784EC406, "Weapon - Stiletto"),
    (0x784EC401, "Weapon - Cinquedea A"),
    (0x784EC410, "Weapon - Cinquedea B"),
    (0x784EC415, "Weapon - Coutelas A"),
    (0x784EC40B, "Weapon - Coutelas B"),
    (0x2E09582D, "Armor - Gauntlets I"),
    (0x2E095832, "Armor - Gauntlets II"),
    (0x2E095837, "Armor - Gauntlets III"),
    (0x2E09583C, "Armor - Gauntlets IV"),
    (0x2E095855, "Armor - Gauntlets V"),
    (0x2E095811, "Armor - Chest I"),
    (0x2E09580C, "Armor - Chest II"),
    (0x2E095807, "Armor - Chest III"),
    (0x2E095802, "Armor - Chest IV"),
    (0x2E09585F, "Armor - Chest V"),
    (0x2E09581C, "Armor - Pauldrons I"),
    (0x2E095817, "Armor - Pauldrons II"),
    (0x2E095821, "Armor - Pauldrons III"),
    (0x2E095826, "Armor - Pauldrons IV"),
    (0x2E095864, "Armor - Pauldrons V"),
    (0xB2186F0C, "Armor - Greaves I"),
    (0x2E0957F7, "Armor - Greaves II"),
    (0x2E0957F8, "Armor - Greaves III"),
    (0xB2186F1D, "Armor - Greaves IV"),
    (0x2E09585A, "Armor - Greaves V"),
    (0x0F07238F, "Armor - Cape Auditore"),
    (0x0F07237E, "Armor - Cape Carnival"),
    (0x0F07237C, "Armor - Cape Firenze"),
    (0x0F072376, "Armor - Cape Neutral"),
    (0x0F07237B, "Armor - Cape Venezia"),
    (0x1F573BB3, "Dye - Assassin White"),
    (0x55308BAB, "Dye - Florentine Crimson"),
    (0x55308BDD, "Dye - Florentine Mahogany"),
    (0x55308BC4, "Dye - Florentine Scarlet"),
    (0x55308BE2, "Dye - Tuscan Copper"),
    (0x55308BA6, "Dye - Tuscan Ember"),
    (0x55308BB0, "Dye - Tuscan Emerald"),
    (0x55308BE7, "Dye - Tuscan Ochre"),
    (0x55308BA1, "Dye - Venetian Azure"),
    (0x55308BBA, "Dye - Venetian Teal"),
    (0x55308B8A, "Dye - Venetian Wine"),
    (0x55308BBF, "Dye - Wetlands Auburn"),
    (0x55308BCE, "Dye - Wetlands Ebony"),
    (0x55308BB5, "Dye - Wetlands Steele"),
    (0x55308BD3, "Dye - Wetlands Alabaster"),
    (0x55308E7F, "Pouch - Knife Belt I"),
    (0x55308E80, "Pouch - Knife Belt II"),
    (0x55308E81, "Pouch - Knife Belt III"),
    (0x55308E83, "Pouch - Medicine Pouch I"),
    (0x55308EA5, "Pouch - Medicine Pouch II"),
    (0x55308EA6, "Pouch - Medicine Pouch III"),
    (0x61D961DF, "Pouch - Poison Vial II"),
    (0x61D961DE, "Pouch - Poison Vial III"),
    (0x55308EA7, "Pouch - Smoke Pouch I"),
    (0x55308EA9, "Pouch - Smoke Pouch III"),
    (0x4787A810, "Painting 1"),
    (0x4787A8BB, "Painting 2"),
    (0x4787A870, "Painting 3"),
    (0x4787A8B6, "Painting 4"),
    (0x4787A87A, "Painting 5"),
    (0x4787A875, "Painting 6"),
    (0x4787A884, "Painting 7"),
    (0x4787A87F, "Painting 8"),
    (0x4787A889, "Painting 9"),
    (0x4787A89D, "Painting 10"),
    (0x4787A898, "Painting 11"),
    (0x4787A8A2, "Painting 12"),
    (0x4787A893, "Painting 13"),
    (0x4787A88E, "Painting 14"),
    (0x4787A84D, "Painting 15"),
    (0x4787A8B1, "Painting 16"),
    (0x4787A8AC, "Painting 17"),
    (0x4787A8A7, "Painting 18"),
    (0x4787A85C, "Painting 19"),
    (0x4787A857, "Painting 20"),
    (0x4787A852, "Painting 21"),
    (0x4787A86B, "Painting 22"),
    (0x4787A861, "Painting 23"),
    (0x4787A866, "Painting 24"),
    (0x4787A839, "Painting 25"),
    (0x4787A83E, "Painting 26"),
    (0x4787A843, "Painting 27"),
    (0x4787A848, "Painting 28"),
    (0x4787A82F, "Painting 29"),
    (0x4787A834, "Painting 30"),
]

SHOP_REGION = "Monteriggioni"
SHOP_ITEM_NAMES: list[str] = [name for _fid, name in SHOP_EQUIPMENT]

# Categories derived by prefix (used by Items.py for the useful/filler classification).
WEAPONS: list[str] = [n for n in SHOP_ITEM_NAMES if n.startswith("Weapon")]
ARMOR_PIECES: list[str] = [n for n in SHOP_ITEM_NAMES if n.startswith("Armor")]
DYES: list[str] = [n for n in SHOP_ITEM_NAMES if n.startswith("Dye")]
PAINTINGS: list[str] = [n for n in SHOP_ITEM_NAMES if n.startswith("Painting")]
POUCHES: list[str] = [n for n in SHOP_ITEM_NAMES if n.startswith("Pouch")]


def _build_shop_locations() -> Dict[str, AC2LocationData]:
    return {f"Shop - {name}": AC2LocationData(SHOP_BASE + i, SHOP_REGION)
            for i, (_fid, name) in enumerate(SHOP_EQUIPMENT)}


SHOP_LOCATIONS: Dict[str, AC2LocationData] = _build_shop_locations()

# --- Villa Renovations (10) ---------------------------------------------
# Building names from docs/02 / docs/data/items-shop-db.md. One check per
# building (not per renovation tier) per task scope. All in Monteriggioni.
VILLA_BUILDINGS: list[str] = [
    "Blacksmith", "Tailor", "Art Merchant", "Bank", "Doctor",
    "Church", "Military Barracks", "Thieves Guild", "Brothel", "Well",
]


def _build_villa_locations() -> Dict[str, AC2LocationData]:
    locations: Dict[str, AC2LocationData] = {}
    for i, name in enumerate(VILLA_BUILDINGS):
        locations[f"Villa - {name}"] = AC2LocationData(VILLA_BASE + i, "Monteriggioni")
    return locations


VILLA_LOCATIONS: Dict[str, AC2LocationData] = _build_villa_locations()

# --- Treasure Chests (330) ---------------------------------------------------
# Named BY DISTRICT (docs/re/chest-districts.json, extracted from the forge
# TreasureChest_<District>.xml files): "Treasure Chest - <District> #k". The region is the
# district's -> an AP hint tells which city/district the chest is in (useful in multiworld).
# The order (district + rank) must stay identical on the gen_map.py side (maps forge ID -> this name).
# The counts sum to 330 (validated at extraction).
CHEST_DISTRICT_ORDER: list[tuple[str, str, int]] = [
    ("Firenze - Admin", "Florence", 27),
    ("Firenze - Commercial", "Florence", 19),
    ("Firenze - Cultural", "Florence", 25),
    ("Toscana", "Tuscany", 46),
    ("Mountains", "Tuscany", 16),
    ("Wetlands", "Forli", 45),
    ("Venezia - Admin", "Venice", 24),
    ("Venezia - Commercial", "Venice", 29),
    ("Venezia - Cultural", "Venice", 24),
    ("Venezia - Industrial", "Venice", 24),
    ("Venezia - Military", "Venice", 24),
    ("Villa", "Monteriggioni", 27),
]
CHEST_COUNT = sum(c for _, _, c in CHEST_DISTRICT_ORDER)  # 330


def _build_chest_locations() -> Dict[str, AC2LocationData]:
    locations: Dict[str, AC2LocationData] = {}
    i = 0
    for label, region, count in CHEST_DISTRICT_ORDER:
        for k in range(1, count + 1):
            locations[f"Treasure Chest - {label} #{k}"] = AC2LocationData(CHESTS_BASE + i, region)
            i += 1
    return locations


CHEST_LOCATIONS: Dict[str, AC2LocationData] = _build_chest_locations()

# --- Monteriggioni statuettes (8) -------------------------------------------
# The 8 Roman statues in the Villa. Detected by PRESENCE of the forge ID in the save
# (Statue_Codex_01..08). All in Monteriggioni.
STATUE_COUNT = 8


def _build_statue_locations() -> Dict[str, AC2LocationData]:
    return {f"Statue #{i + 1}": AC2LocationData(STATUES_BASE + i, "Monteriggioni")
            for i in range(STATUE_COUNT)}


STATUE_LOCATIONS: Dict[str, AC2LocationData] = _build_statue_locations()

# --- Codex pages (30) --------------------------------------------------------
# "By count" design (like viewpoints): the Nth page picked up sends "Codex Page #N".
# Detection: "acquired item" records whose ID falls in the codex ranges (0x4658D3xx /
# 0x45B9E6xx, verified codex-only against the catalog + save). Round-robin over regions
# (pages spread across all cities in vanilla).
CODEX_REGIONS = ("Florence", "Tuscany", "Forli", "Venice")
CODEX_LOCATION_COUNT = 30


def _build_codex_locations() -> Dict[str, AC2LocationData]:
    return {f"Codex Page #{i + 1}": AC2LocationData(CODEX_BASE + i,
                                                    CODEX_REGIONS[i % len(CODEX_REGIONS)])
            for i in range(CODEX_LOCATION_COUNT)}


CODEX_LOCATIONS: Dict[str, AC2LocationData] = _build_codex_locations()

# --- Collectible districts (reliable design, detected via mission record) ----
# Each district is a Mission in the engine (docs/re/collectible-ids.txt):
# completing all feathers/chests of the district completes the mission -> save record.
# Dedicated sub-ranges to avoid colliding with the individual locations.
FEATHER_DISTRICTS_BASE = BASE_ID + 8000
CHEST_DISTRICTS_BASE = BASE_ID + 8100

FEATHER_DISTRICTS: list[tuple[str, str]] = [
    ("Firenze - Admin", "Florence"), ("Firenze - Commercial", "Florence"),
    ("Firenze - Cultural", "Florence"), ("Toscana", "Tuscany"),
    ("Venezia - Admin", "Venice"), ("Venezia - Commercial", "Venice"),
    ("Venezia - Cultural", "Venice"), ("Venezia - Industrial", "Venice"),
    ("Venezia - Military", "Venice"), ("Villa", "Monteriggioni"),
    ("Wetlands", "Forli"),
]
CHEST_DISTRICTS: list[tuple[str, str]] = [
    ("Firenze - Admin", "Florence"), ("Firenze - Commercial", "Florence"),
    ("Firenze - Cultural", "Florence"), ("Mountains", "Tuscany"),
    ("Toscana", "Tuscany"), ("Venezia - Admin", "Venice"),
    ("Venezia - Commercial", "Venice"), ("Venezia - Cultural", "Venice"),
    ("Venezia - Industrial", "Venice"), ("Venezia - Military", "Venice"),
    ("Villa", "Monteriggioni"), ("Wetlands", "Forli"),
]


def _build_district_locations(items, base, prefix) -> Dict[str, AC2LocationData]:
    return {f"{prefix} - {name}": AC2LocationData(base + i, region)
            for i, (name, region) in enumerate(items)}


FEATHER_DISTRICT_LOCATIONS = _build_district_locations(
    FEATHER_DISTRICTS, FEATHER_DISTRICTS_BASE, "Feathers")
CHEST_DISTRICT_LOCATIONS = _build_district_locations(
    CHEST_DISTRICTS, CHEST_DISTRICTS_BASE, "Treasure Chests")

# Option name (AC2Options field) -> its location dict. Used by both
# location_name_to_id (always the full universe, required by AP regardless of
# a given player's options) and by create_regions/create_items (which only
# instantiate the categories actually toggled on for that player - see
# Regions.py:create_regions). main_missions has no toggle-off in this design
# (matches existing MVP behavior: always included) so it's not in this table.
# feathers/chests are NOT here: their location set depends on an extra option
# (feather_sanity/chest_sanity), handled separately in build_active_location_table.
OPTIONAL_LOCATION_CATEGORIES: Dict[str, Dict[str, AC2LocationData]] = {
    "viewpoints": VIEWPOINT_LOCATIONS,
    "glyphs": GLYPH_LOCATIONS,
    "secondary_missions": SECONDARY_MISSION_LOCATIONS,
    "tombs": TOMB_LOCATIONS,
    "shop_items": SHOP_LOCATIONS,
    "villa_renovations": VILLA_LOCATIONS,
    "statues": STATUE_LOCATIONS,
    "codex_pages": CODEX_LOCATIONS,
}

# Full universe of every location this world could ever create, across all
# options. Required by AP's World.location_name_to_id contract (static,
# option-independent). Do not use this for actual region/item creation.
LOCATION_TABLE: Dict[str, AC2LocationData] = dict(MAIN_MISSION_LOCATIONS)
for _category in OPTIONAL_LOCATION_CATEGORIES.values():
    LOCATION_TABLE.update(_category)
# feathers/chests: both sets (individual + district) in the full universe.
for _category in (FEATHER_LOCATIONS, FEATHER_DISTRICT_LOCATIONS,
                  CHEST_LOCATIONS, CHEST_DISTRICT_LOCATIONS):
    LOCATION_TABLE.update(_category)

LOCATION_NAME_TO_ID: Dict[str, int] = {name: data.id for name, data in LOCATION_TABLE.items()}

GOAL_LOCATION_NAME = "Sequence 14 - In Bocca al Lupo"


def build_active_location_table(options) -> Dict[str, AC2LocationData]:
    """Locations actually in play for one world, gated by its Options toggles."""
    active: Dict[str, AC2LocationData] = dict(MAIN_MISSION_LOCATIONS)
    for option_name, category in OPTIONAL_LOCATION_CATEGORIES.items():
        if getattr(options, option_name).value:
            active.update(category)
    # feathers/chests: district (reliable, default) or individual depending on the chosen sanity.
    if options.feathers.value:
        active.update(FEATHER_LOCATIONS if options.feather_sanity.value
                      else FEATHER_DISTRICT_LOCATIONS)
    if options.chests.value:
        active.update(CHEST_LOCATIONS if options.chest_sanity.value
                      else CHEST_DISTRICT_LOCATIONS)
    return active


def location_name_to_region(name: str) -> Optional[str]:
    data = LOCATION_TABLE.get(name)
    return data.region if data else None
