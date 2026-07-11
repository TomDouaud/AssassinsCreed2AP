"""Region graph for AC2AP, derived from docs/03-logic.md.

Simplified MVP graph: one region per city/zone, no per-district sub-regions.
Access is gated purely by Progressive Sequence count, matching where each
zone's missions first become available in vanilla AC2.
"""
from __future__ import annotations

from typing import TYPE_CHECKING, Dict

from BaseClasses import Region

from .Locations import build_active_location_table

if TYPE_CHECKING:
    from . import AC2World

MENU_REGION = "Menu"

# Region name -> minimum Progressive Sequence count required to enter.
# Sequence 1 is the start state (0 Progressive Sequence items owned).
REGION_SEQUENCE_REQUIREMENT: Dict[str, int] = {
    "Florence": 0,       # Sequences 1-4 and 13 (DLC)
    "Monteriggioni": 2,  # opens with Sequence 3
    "Tuscany": 4,        # Sequences 5-6
    "Forli": 11,         # Sequence 12 (DLC)
    "Venice": 6,         # Sequences 7-11
    "Rome": 13,          # Sequence 14 only, final gate
}

REGION_NAMES = (MENU_REGION,) + tuple(REGION_SEQUENCE_REQUIREMENT)


def create_regions(world: "AC2World") -> None:
    # Deferred import: __init__.py imports this module at load time, so importing
    # AC2Location at module scope here would create a circular import.
    from . import AC2Location

    multiworld = world.multiworld
    player = world.player

    regions = {name: Region(name, player, multiworld) for name in REGION_NAMES}

    active_locations = build_active_location_table(world.options)
    world.active_locations = active_locations

    for location_name, data in active_locations.items():
        region = regions[data.region]
        region.locations.append(AC2Location(player, location_name, data.id, region))

    for region in regions.values():
        multiworld.regions.append(region)

    # AC2 story order: Florence -> Tuscany -> Venice -> Forli (late DLC) -> Rome.
    # Monteriggioni branches off Florence. NOTE: the graph MUST follow the sequence-requirement
    # order (REGION_SEQUENCE_REQUIREMENT), otherwise a region gets over-gated by a region
    # traversed upstream (fixed: Forli req 11 used to come before Venice req 6).
    regions[MENU_REGION].connect(regions["Florence"])
    regions["Florence"].connect(regions["Monteriggioni"])
    regions["Florence"].connect(regions["Tuscany"])
    regions["Tuscany"].connect(regions["Venice"])
    regions["Venice"].connect(regions["Forli"])
    regions["Forli"].connect(regions["Rome"])
