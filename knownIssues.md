# Known Issues & Limitations

This is an early alpha. Known limitations:

- **Codex pages register when decoded.** Each of the 30 pages is its own check, read
  from the save. A page counts once Leonardo has decoded it rather than at the moment
  you open the chest, so the check can arrive a little after the pickup.
- **Glyphs now work** (20 locations, `glyphs` option). They are the one collectible the
  game never writes to the save, so the client reads their solved state live from the
  Animus Database: the game must be running for a solved glyph to register.
- **Assassin Tombs work** (6 locations, `tombs` option) - completing a tomb sends its
  check, verified against saves at several points of the story.
- **Secondary missions stay off.** 43 of the 45 are detected, but two contracts
  (Caveat Emptor, Zero Tolerance) have no known game id and could never be checked -
  enabling the category could strand a required item and make a seed impossible.
- **A few locations exist but can never be checked**, because no game id is known for
  them: `Sequence 5 - Four to the Floor` (Sequence 5's memories share one record block
  and cannot be told apart), the two contracts above, and 9 of the 10 villa renovations.
  They are marked EXCLUDED, so the generator only ever puts filler on them and a seed
  can always be finished - you will simply never collect those particular checks.
- **Reserved-but-inactive options.** Viewpoints and villa renovations have option
  toggles, but enabling them currently has no effect (detection not yet reliable).
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
- **Weapon/equipment grant.** Shop equipment (weapons, armor, dyes, paintings,
  pouches) circulates in the multiworld — you can send and receive it, and it counts
  for the seed — but receiving a piece does **not** put it in Ezio's inventory yet.
  You will only carry what you actually bought in-game. This is not tied to story
  progress: finishing a sequence changes nothing. Received-item toasts say
  "[not granted in-game yet]" so it's unambiguous.
  Why: a weapon is not a value we can write, it is an entity the engine builds
  (object pool + handle + vtable). Both routes were explored and hit that wall —
  live RAM (hardware breakpoint on the inventory during an equip, which lands in the
  generic pool allocator) and save editing (the save carries the item in two separate
  lists plus per-instance handles). Florins, consumables and traps *are* applied
  in-game, because those are plain values.
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
