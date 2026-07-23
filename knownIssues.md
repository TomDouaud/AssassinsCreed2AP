# Known Issues & Limitations

This is an early alpha. Known limitations:

- **Reserved-but-inactive options.** Viewpoints, glyphs, Assassin Tombs, secondary
  missions and villa renovations have option toggles, but enabling them currently
  has no effect (detection not yet reliable).
- **Sequence 2 mission checks fire in the wrong order.** Two of the game records used
  for Sequence 2 (`397E179C` / `397E179F`) are not emitted when their mission is
  completed: one fires in the cascade at the *end of Sequence 1*, the other when you
  *synchronise a viewpoint* near Paola's. So the first Sequence 2 checks are sent
  early and under the wrong names; from "Laying Low" onward it behaves normally.
  The records themselves are real (nothing is lost — every location still gets sent),
  only the pairing of record → mission name is off for the first two. Root cause is
  known (these ids look like sequence-transition "beats", flagged as unidentified in
  our reverse-engineering notes) and needs a dedicated Sequence 2 capture to fix.
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
- **DeathLink emission covers combat and instant deaths (falls).** Two complementary
  detectors: a hook on the game's SetHealth routine catches instant deaths (negative HP),
  and health-object invalidation catches combat deaths. Guard kills do not false-positive,
  and a received death is not echoed back. Exotic scripted desyncs (mission-failure
  teleports) may not emit — arguably correct behavior.
- **Build target.** Verified on the 1.01 build; other builds are untested.

Found a bug? Please open an issue on the GitHub repository.
