"""Access rules for AC2AP regions and the victory goal."""
from __future__ import annotations

import re
from typing import TYPE_CHECKING

from worlds.generic.Rules import set_rule

from .Items import PROGRESSIVE_SEQUENCE
from .Locations import GOAL_LOCATION_NAME
from .Regions import REGION_SEQUENCE_REQUIREMENT

if TYPE_CHECKING:
    from . import AC2World


def has_sequences(state, player: int, count: int) -> bool:
    if count <= 0:
        return True
    return state.has(PROGRESSIVE_SEQUENCE, player, count)


def set_region_rules(world: "AC2World") -> None:
    # Optional city-level gating (region_gating option). OFF = everything open from the start.
    # Finer per-district gating (closer to the real in-game unlock order) is feasible but needs
    # the district->sequence data: future refinement.
    if not world.options.region_gating:
        return

    multiworld = world.multiworld
    player = world.player

    for region in multiworld.get_regions(player):
        if region.name not in REGION_SEQUENCE_REQUIREMENT:
            continue
        required = REGION_SEQUENCE_REQUIREMENT[region.name]
        for entrance in region.entrances:
            set_rule(entrance, lambda state, required=required: has_sequences(state, player, required))

    # Per-mission story gating: a "Sequence N - ..." mission can only be played once the
    # story has reached sequence N, i.e. with N-1 Progressive Sequences. Without this the
    # region graph alone under-gates (e.g. Florence hosts Sequences 1-4 AND 13, so a Seq 13
    # mission would be considered reachable from the start).
    for location in multiworld.get_locations(player):
        m = re.match(r"Sequence (\d+) - ", location.name)
        if not m:
            continue
        required = int(m.group(1)) - 1
        if required <= 0:
            continue
        set_rule(location, lambda state, required=required: has_sequences(state, player, required))


def set_goal_rule(world: "AC2World") -> None:
    multiworld = world.multiworld
    player = world.player

    # Goal = reach Sequence 14. Vanilla AC2 already requires all 30 Codex pages to unlock
    # the finale, so an extra AP "required codex items" gate would be redundant and confusing
    # (you physically can't finish Seq 14 in-game without the pages). Codex pages, when the
    # option is on, are just checks along the way.
    def goal_rule(state) -> bool:
        return has_sequences(state, player, 13)

    goal_location = multiworld.get_location(GOAL_LOCATION_NAME, player)
    set_rule(goal_location, goal_rule)
    multiworld.completion_condition[player] = lambda state: state.can_reach_location(GOAL_LOCATION_NAME, player)
