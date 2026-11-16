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
- **Codex checks arrive when the game next writes a save, not when you decode.** The client
  re-reads the save only when its timestamp changes, so pages you decoded sit undetected until
  the next autosave. That makes them look tied to whatever you happened to be doing at that
  moment - one player decoded four pages, solved a glyph puzzle some time later, and saw all
  four codex checks fire at once and blamed the glyph. Nothing is lost, it is only late.
- **Collectible latency.** Chests/feathers/statues/Codex are detected from the save,
  which the game writes on autosave — a check can take a short moment to register
  (missions are near-instant).
- **Progression items have no in-game effect yet.** Progressive Sequence / Codex /
  Seal / Hidden Blade gate the seed logically on the Archipelago side, but are not
  enforced inside the game (open world stays reachable).
- **Received equipment now lands in Ezio's inventory** (weapons, armour, dyes, pouches,
  paintings). The client calls the game's own inventory routines, the same ones a shop
  purchase uses, so the result behaves like a real purchase and survives save + reload.
  Verified in game: weapons (the sword appears in Ezio's hand), dyes (worn at once), pouches
  (Medicine Pouch II raises the remedy cap). Armour reaches the inventory the way a purchase
  does, one piece per body part - but removing a piece live was seen to leave Ezio's model and
  health unchanged, so a received piece may likewise only show on him once the save is next
  loaded. Being checked.
  Not yet verified by eye: a received painting showing up in the villa gallery.
  - Armour never downgrades: receiving a piece of an equal or lower tier than the one you
    wear for that body part is ignored. Pouches never lower your current tier.
  - Only the last dye received is worn.
  - **Receiving item X also completes the location "Shop - X"** the next time the game
    saves, because you now own X and the shop treats it as bought. Deliberate: blocking it
    would make "Shop - X" unbuyable and strand whatever the seed put there.

- **Equipment the save lost is given back on connect.** If the game closes before the next
  autosave, an item received just before never reaches the save. On each connect the client
  re-checks the equipment it already received and gives back what is missing. Two
  exceptions: dyes are never given back (only the worn dye is kept by the game, so it would
  undo the colour you picked at the tailor), and weapons only while Ezio holds one (an empty
  weapon list is how a mission's scripted disarm looks, and re-arming him would break it).
- **Disarm Trap** takes Ezio's weapons for 5 minutes, then gives them back. What it takes is
  written to `AC2AP_disarm.txt` first, so quitting or crashing mid-trap costs nothing: the
  weapons come back once you are in game again (verified, including a save written while
  disarmed). It needs a seed generated with this version's apworld.
- **Not possible yet:** a trap that strips armour, or takes back the second hidden blade.
  Removing armour from the inventory leaves it on Ezio and in his health, and one blade vs
  two is not an inventory item - both need a routine we have not found.

- **Templar Grip is UNSTABLE / experimental (off by default).** It clamps the
  notoriety *meter* to a floor that Progressive Templar Grip items lower (75% → 50%
  → 25% → 0%). What it does NOT do: force the renegade *state*. In AC2 that state is
  event-driven and computed by the guard AI (not a writable value — confirmed by
  reverse-engineering, see notes), so a "guards hostile from the start" tier isn't
  achievable via the meter. The clamp is capped just below the renegade trigger so it
  stays inert while you are actually renegade (no meter re-pin, no repeated-animation
  bug). Treat as a curiosity, not a balanced mode.
  The floor stays OFF until you have completed **Sequence 2 - Laying Low**: that mission
  makes you go incognito by tearing posters, and a floor re-pinned every tick undid each
  poster, so it could never be finished.
- **Wanted Trap may be inert.** Same root cause: writing the notoriety value without a
  game event does not force the hostile state.
- **DeathLink emission covers combat and instant deaths (falls).** Two complementary
  detectors: a hook on the game's SetHealth routine catches instant deaths (negative HP),
  and health-object invalidation catches combat deaths. Guard kills do not false-positive,
  and a received death is not echoed back. Exotic scripted desyncs (mission-failure
  teleports) may not emit — arguably correct behavior.
- **Build target.** Verified on the 1.01 build; other builds are untested.

Found a bug? Please open an issue on the GitHub repository.
