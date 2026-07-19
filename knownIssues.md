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
- **Templar Grip is UNSTABLE / experimental (off by default).** It clamps the
  notoriety *meter* to a floor that Progressive Templar Grip items lower (75% → 50%
  → 25% → 0%). What it does NOT do: force the renegade *state*. In AC2 that state is
  event-driven and computed by the guard AI (not a writable value — confirmed by
  reverse-engineering, see notes), so a "guards hostile from the start" tier isn't
  achievable via the meter. The clamp is capped just below the renegade trigger so it
  stays inert while you are actually renegade (no meter re-pin, no repeated-animation
  bug). Treat as a curiosity, not a balanced mode.
- **Wanted Trap may be inert.** Same root cause: writing the notoriety value without a
  game event does not force the hostile state.
- **DeathLink emission is best-effort.** Receiving a death (and dying) is reliable, and
  a death received while the game is unfocused is buffered and applied on refocus. But
  *emitting* your own death is detected from the health object, which the engine does not
  update for instant deaths (e.g. a fatal fall) — those can be missed. Combat deaths are
  detected. A reliable emit needs hooking the game's death routine (planned).
- **Build target.** Verified on the 1.01 build; other builds are untested.

Found a bug? Please open an issue on the GitHub repository.
