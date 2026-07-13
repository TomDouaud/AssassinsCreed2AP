# Known Issues & Limitations

This is an early alpha. Known limitations:

- **Reserved-but-inactive options.** Viewpoints, glyphs, Assassin Tombs, secondary
  missions and villa renovations have option toggles, but enabling them currently
  has no effect (detection not yet reliable).
- **Collectible latency.** Chests/feathers/statues/Codex are detected from the save,
  which the game writes on autosave — a check can take a short moment to register
  (missions are near-instant).
- **Progression items have no in-game effect yet.** Progressive Sequence / Codex /
  Seal / Hidden Blade gate the seed logically on the Archipelago side, but are not
  enforced inside the game (open world stays reachable).
- **Weapon/equipment grant.** Equipment items circulate in the multiworld but are
  not yet materialized into Ezio's inventory in-game.
- **Templar Grip is experimental.** The current implementation clamps the notoriety
  meter, but AC2's renegade state is event-driven, so the intended "guards hostile
  until items arrive" behavior is not delivered yet. The mechanic is being reworked
  (state forcing); keep the option off for normal play.
- **Wanted Trap may be inert.** Same root cause: writing the notoriety value without
  a game event does not trigger the hostile state. Under investigation.
- **Build target.** Verified on the 1.01 build; other builds are untested.

Found a bug? Please open an issue on the GitHub repository.
