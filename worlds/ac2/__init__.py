"""AC2AP - Archipelago world for Assassin's Creed II.

The 86 main story missions are locations, progression is gated by Progressive
Sequence items, and Sequence 14 is the default goal. Collectibles (chests,
feathers, statues), shop purchases and Codex pages are additional check
categories behind their options. See the project README for feature status.
"""
from __future__ import annotations

from typing import Any, ClassVar, Dict

from BaseClasses import Item, ItemClassification, Location, Tutorial
from worlds.AutoWorld import WebWorld, World

from .Items import (
    FILLER_ITEMS,
    ITEM_NAME_TO_ID,
    ITEM_TABLE,
    PROGRESSION_ITEMS,
    PROGRESSIVE_TEMPLAR_GRIP,
    SHOP_ITEM_NAMES,
    TRAP_ITEMS,
    USEFUL_ITEMS,
)
from .Locations import GOAL_LOCATION_NAME, LOCATION_NAME_TO_ID, LOCATION_TABLE
from .Options import AC2Options
from .Regions import create_regions
from .Rules import set_goal_rule, set_region_rules


class AC2Location(Location):
    game = "Assassin's Creed II"


class AC2Item(Item):
    game = "Assassin's Creed II"


class AC2Web(WebWorld):
    theme = "dirt"
    tutorials = [
        Tutorial(
            "Multiworld Setup Guide",
            "A guide to setting up AC2AP for Archipelago.",
            "English",
            "setup_en.md",
            "setup/en",
            ["AC2AP contributors"],
        )
    ]


class AC2World(World):
    """Assassin's Creed II is a 2009 action-adventure game following Ezio Auditore's
    quest for vengeance and truth across Renaissance Italy."""

    game = "Assassin's Creed II"
    web = AC2Web()
    options_dataclass = AC2Options
    options: AC2Options

    item_name_to_id: ClassVar[Dict[str, int]] = ITEM_NAME_TO_ID
    location_name_to_id: ClassVar[Dict[str, int]] = LOCATION_NAME_TO_ID

    location_name_to_location = LOCATION_TABLE

    def create_regions(self) -> None:
        create_regions(self)

    def create_item(self, name: str) -> AC2Item:
        data = ITEM_TABLE[name]
        return AC2Item(name, data.classification, data.id, self.player)

    def create_items(self) -> None:
        item_pool: list[AC2Item] = []

        for name in PROGRESSION_ITEMS + USEFUL_ITEMS:
            count = ITEM_TABLE[name].count
            item_pool.extend(self.create_item(name) for _ in range(count))

        # Equipment items: circulate in the multiworld (counterpart of the shop locations).
        # Created only when shop_items is on, otherwise there would be more items than
        # locations. One per name = the matching object.
        if self.options.shop_items.value:
            item_pool.extend(self.create_item(name) for name in SHOP_ITEM_NAMES)

        # Templar Grip: one item per 25% step of the starting floor (100 -> 4 items).
        if self.options.templar_grip.value:
            n_grip = self.options.templar_grip_start.value // 25
            item_pool.extend(self.create_item(PROGRESSIVE_TEMPLAR_GRIP) for _ in range(n_grip))

        remaining = len(self.multiworld.get_unfilled_locations(self.player)) - len(item_pool)
        remaining = max(remaining, 0)

        # trap_fill % of the filler is replaced by traps (uniform distribution)
        trap_ratio = self.options.trap_fill.value / 100
        n_traps = round(remaining * trap_ratio)
        for i in range(n_traps):
            item_pool.append(self.create_item(TRAP_ITEMS[i % len(TRAP_ITEMS)]))
        for i in range(remaining - n_traps):
            item_pool.append(self.create_item(FILLER_ITEMS[i % len(FILLER_ITEMS)]))

        self.multiworld.itempool += item_pool

    def set_rules(self) -> None:
        set_region_rules(self)
        set_goal_rule(self)

    def fill_slot_data(self) -> Dict[str, Any]:
        return {
            "goal": self.options.goal.value,
            "required_codex_pages": self.options.required_codex_pages.value
            if self.options.codex_pages else 0,
            "death_link": bool(self.options.death_link.value),
            "templar_grip": self.options.templar_grip.value,
            "templar_grip_start": self.options.templar_grip_start.value
            if self.options.templar_grip else 0,
        }

    def get_filler_item_name(self) -> str:
        return FILLER_ITEMS[0]


__all__ = ["AC2World", "AC2Location", "AC2Item"]
