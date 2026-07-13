"""Player-facing options for AC2AP."""
from __future__ import annotations

from dataclasses import dataclass

from Options import Choice, DeathLink, DefaultOnToggle, PerGameCommonOptions, Range, Toggle


class MainMissions(Toggle):
    """Include the 86 main story missions (Sequences 1-14) as locations."""
    display_name = "Main Missions"
    default = 1


class SecondaryMissions(Toggle):
    """Include assassination contracts, races, courier and beat-up missions as locations.

    Not yet implemented in this MVP: enabling this has no effect.
    """
    display_name = "Secondary Missions"
    default = 0


class Viewpoints(Toggle):
    """Include synchronizing viewpoints as locations.

    Not yet reliable (BUG-005 still open) : the candidate save record type is shared with tombs /
    missions / other discovery events, so counting over-fires. Enabling has no effect until the
    real per-viewpoint discriminant is found (DataBlocks). Kept off.
    """
    display_name = "Viewpoints"
    default = 0


class Feathers(Toggle):
    """Include collecting feathers as locations.

    Not yet implemented in this MVP: enabling this has no effect.
    """
    display_name = "Feathers"
    default = 0


class Glyphs(Toggle):
    """Include solving glyph puzzles as locations.

    Not yet implemented in this MVP: enabling this has no effect.
    """
    display_name = "Glyphs"
    default = 0


class CodexPages(Toggle):
    """Include finding Codex pages as locations.

    Count-based : the Nth codex page picked up sends "Codex Page #N" (detected via the
    acquired-item save records in the codex id ranges, 06/07).
    """
    display_name = "Codex Pages"
    default = 0


class Tombs(Toggle):
    """Include Assassin Tombs (seal rooms) as locations.

    Not yet implemented in this MVP: enabling this has no effect.
    """
    display_name = "Assassin Tombs"
    default = 0


class Chests(Toggle):
    """Include treasure chests as locations.

    district (default sanity): per-district checks via save. individual: per-chest via presence
    of the chest's forge id in the save (chantier 1, 06/07).
    """
    display_name = "Treasure Chests"
    default = 0


class ShopItems(Toggle):
    """Include shop purchases (blacksmith, tailor, art dealer) as locations.

    Not yet implemented in this MVP: enabling this has no effect.
    """
    display_name = "Shop Items"
    default = 1


class VillaRenovations(Toggle):
    """Include Monteriggioni villa renovations as locations.

    Not yet implemented in this MVP: enabling this has no effect.
    """
    display_name = "Villa Renovations"
    default = 0


class FeatherSanity(Choice):
    """How feathers become checks.

    district (default): one check per feather district (11 total), completed by collecting
      all feathers in that district. Detected reliably via the game's save (feather districts
      are missions in the engine). Works today.
    individual: one check per feather (100 total). Detected via the save : each feather's
      completion record carries its forge item id, mapped 1:1 to a location (chantier 1, 06/07).
    """
    display_name = "Feather Sanity"
    option_district = 0
    option_individual = 1
    default = 0


class ChestSanity(Choice):
    """How treasure chests become checks. Same tradeoff as Feather Sanity.

    district (default): one check per chest district (12 total). Reliable via save.
    individual: one check per chest (330 total). Detected by presence of the chest's forge item
      id in the save (chantier 1, 06/07).
    """
    display_name = "Chest Sanity"
    option_district = 0
    option_individual = 1
    default = 0


class Goal(Choice):
    """The victory condition for this playthrough.

    rodrigo: complete Sequence 14 - In Bocca al Lupo (default, only goal implemented in this MVP).
    codex_hunt: not yet implemented, falls back to rodrigo.
    full_sync: not yet implemented, falls back to rodrigo.
    assassino: not yet implemented, falls back to rodrigo.
    """
    display_name = "Goal"
    option_rodrigo = 0
    option_codex_hunt = 1
    option_full_sync = 2
    option_assassino = 3
    default = 0


class TrapFill(Range):
    """Percentage of filler items replaced by traps (0 = none).

    Available traps: Templar Tax (-25% florins), Bad Medicine (health to 1),
    Wanted (notoriety maxed, guards hunt you down).
    """
    display_name = "Trap Fill Percentage"
    range_start = 0
    range_end = 100
    default = 0


class RequiredCodexPages(Choice):
    """How many Codex Page items are required to unlock Sequence 14.

    Only applies when the codex_pages option is enabled; ignored otherwise (gate stays open).
    """
    display_name = "Required Codex Pages"
    option_all_30 = 30
    option_20 = 20
    option_10 = 10
    default = 30


class Statues(Toggle):
    """Include the 8 Monteriggioni statuettes as locations.

    Detected via the save : each statuette's forge item id appears when collected (presence-based,
    chantier 1, 06/07).
    """
    display_name = "Monteriggioni Statues"
    default = 0


class TemplarGrip(Choice):
    """Reverse-notoriety challenge: your notoriety is clamped to a floor that only
    Progressive Templar Grip items can lower (25% per item, down to vanilla).

    off (default): vanilla notoriety.
    global_grip: one floor for the whole game (a per-region variant is planned once
      in-RAM region detection is done).
    Requires the client's RAM hooks (enable_health_hook=1 in AC2AP.ini).
    """
    display_name = "Templar Grip"
    option_off = 0
    option_global_grip = 1
    default = 0


class TemplarGripStart(Choice):
    """Starting notoriety floor when Templar Grip is on.

    100 (default) = maxed notoriety from the start: guards attack on sight until you
    receive Progressive Templar Grip items. Lower values soften the challenge and
    reduce how many Progressive Templar Grip items are added to the pool (one per
    25% step).
    """
    display_name = "Templar Grip Starting Floor"
    option_100 = 100
    option_75 = 75
    option_50 = 50
    option_25 = 25
    default = 100


class RegionGating(DefaultOnToggle):
    """Gate content by story progression (Progressive Sequence items).

    on (default) : each city region requires a minimum number of Progressive Sequences to enter,
      matching the vanilla story order (Florence -> Monteriggioni -> Tuscany -> Venice -> Forli ->
      Rome). The generator places items so the seed stays beatable along that order.
    off : all regions are reachable from the start — items can be placed anywhere (open/rando mode).
    Note (06/07) : gating is CITY-level. Finer DISTRICT-level gating (each district opens at its own
    sequence) is feasible but needs the district->sequence data ; noted as a future refinement.
    """
    display_name = "Region Gating"


@dataclass
class AC2Options(PerGameCommonOptions):
    goal: Goal
    required_codex_pages: RequiredCodexPages
    main_missions: MainMissions
    secondary_missions: SecondaryMissions
    viewpoints: Viewpoints
    feathers: Feathers
    feather_sanity: FeatherSanity
    glyphs: Glyphs
    codex_pages: CodexPages
    tombs: Tombs
    chests: Chests
    chest_sanity: ChestSanity
    shop_items: ShopItems
    villa_renovations: VillaRenovations
    statues: Statues
    region_gating: RegionGating
    templar_grip: TemplarGrip
    templar_grip_start: TemplarGripStart
    death_link: DeathLink
    trap_fill: TrapFill
