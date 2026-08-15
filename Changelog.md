# Changelog

## 0.1.7 — alpha

- **Fixed: checks fired when you STARTED a mission instead of finishing it** — one
  memory ahead of you, which also made Sequence 2 look out of order. The game writes
  a mission record as soon as the memory becomes available and only marks it done on
  completion; we were reporting it on sight. The per-district collectible bundles had
  the same problem and were firing on district entry. Verified in-game.
- **Glyph puzzles are now supported** (20 locations, `glyphs` option). Glyphs are the
  one collectible the game never writes to the save, so their solved state is read
  live from the Animus Database — meaning the game has to be running for one to
  register.
- **Fixed: the client was starting up to 35 times per launch.** The ASI loader also
  injects into the launcher and Uplay helper processes next to the game, and each
  one started its own client: several connections for one slot, all writing the same
  state files. Only the game process runs the client now.
- **Save detection**: the client watches the *most recently modified* save instead of
  assuming `1.save`, also looks inside `<game folder>\savegames\` (a layout the Uplay
  wrapper creates), and warns loudly when the save it picked looks empty. Watching the
  wrong file is the most common cause of "it connects and receives items but never
  sends anything".
- Received shop equipment now says `[not granted in-game yet]`, so nobody hunts for
  gear that will not appear.
- Location checks are retried with a backoff until the server acknowledges them.

## 0.1.6 — alpha

- **Fixed: checks only registering after reconnecting.** A websocket can die
  silently (NAT / idle timeout on a hosted room — a LAN server never shows it):
  the send succeeds locally, the server never receives it, and no error is raised.
  Checks now stay queued until the **server acknowledges** them and are retried
  every 10 s, so they can no longer be lost between reconnects. This also explains
  shop purchases (e.g. the medium/large medicine pouches) appearing to do nothing.
- Docs: how to identify **which save file is actually yours** — it is not always
  `1.save`. Watching the wrong file connects and receives items but never sends a
  single check.

## 0.1.5 — alpha

- No more scary red "TLS handshake failed" toast when connecting to a plain-ws
  server (localhost, or a non-TLS room): the client tries `wss://` then falls back
  to `ws://`, and that first failed attempt is normal — it's now silent (logged only).
- Startup log now records the save file size (`baseline: ... save size=N bytes`), so
  a wrong-slot / near-empty save is obvious from the log.
- Docs: turn OFF Ubisoft Connect cloud save sync (it overwrites the local save at
  launch on any version), and play on save slot 1 (or point `save_path` at 2/3.save).

## 0.1.4 — alpha

- Overlay layout: the F8 menu now has an **"Overlay layout"** section to pick which
  screen corner the **toasts** and the **status line** anchor to (any of the four
  corners). Applies live and is saved to the ini (`toast_corner` / `status_corner`).
- Confirmed working on the **Steam** build of the game (in addition to Ubisoft
  Connect and Skidrow) — no separate build needed.

## 0.1.3 — alpha

- **Fixed: "Can't reach server: End of file" on archipelago.gg-hosted rooms.** The
  client now speaks TLS (WSS) — webhosted rooms work; validated live. (The old build
  could only reach plain `ws://` servers like localhost.)
- Fixed: the shipped ini pre-filled a placeholder server/slot, so the client
  auto-connected to a non-existent room at launch and popped that same cryptic
  toast. The template now ships blank, the placeholder never auto-connects, and
  socket errors explain themselves ("server closed the connection (wrong port, or
  room not running?) — F8 to edit").
- Reliable DeathLink emission: a hook on the game's SetHealth routine (found via
  hardware breakpoint + Ghidra decompilation) catches instant deaths (falls) that the
  health poll missed; combat deaths keep the invalidation detector. Guard kills don't
  false-positive; a received death is not echoed back (10 s suppression window).
- New filler items with a real in-game effect: Smoke Bombs (+3), Medicine (+2),
  Poison Vials (+2), Gun Ammo (+6) — granted straight into Ezio's pouches.
- Docs: point `save_path` at the `1.save` FILE (not its folder), and play until the
  first autosave before connecting (a fresh install has no save to find yet).

## 0.1.2 — alpha

- "Save found" / "SAVE NOT FOUND" toast at startup; Ubisoft Connect save path fixed
  (`Program Files (x86)\Ubisoft\...\4\1.save`, first profile), no-quotes note.
- Fix #1: double text input in the F8 menu on Ubisoft Connect.
- DeathLink: apply a received death only while the game is focused (unfocused was
  losing it), and emit on a real death (low-HP then the health object desyncs) instead
  of the survivable 0-HP warning (#2). Emission is best-effort (an instant fall can be
  missed); receiving/buffering is reliable.
- Overlay: received-item toasts colored by AP classification (trap/progression/useful/
  filler), longer toast duration, and a bottom-left status line. F9 cycles it: off →
  overview (checks/items) → per-category breakdown (done/total for missions, feathers,
  chests, viewpoints, ...), filtered to the categories enabled in your YAML.
- On connect, every check already done in the save is re-sent (idempotent) and counted,
  so the counters are retroactive and a fresh seed / restarted server is re-synced.

## 0.1.1 — alpha

- In-game overlay: connection menu (F8/INSERT) with server/slot/password fields,
  toasts for received/sent items, connection, goal and errors.
- "Save found" / "SAVE NOT FOUND" toast at startup so you know detection worked.
- Ubisoft Connect save auto-detect fixed (searches `Program Files (x86)\Ubisoft\
  Ubisoft Game Launcher\savegames`; uses the first profile's `1.save`).
- Fix: double text input in the F8 menu on the Ubisoft Connect version (#1).
- Removed the redundant `required_codex_pages` option (vanilla already needs 30).

## 0.1.0 — alpha-alpha (first public release)

- Archipelago world for Assassin's Creed II (`worlds/ac2/`).
- ASI client (C++) with an embedded Archipelago client (apclientpp): save-file
  check detection, item application, DeathLink, and goal completion reporting
  (ClientStatus::GOAL when Sequence 14 is finished).
- Enabled check categories: main story missions, treasure chests, feathers,
  Monteriggioni statues, shop purchases, Codex pages.
- Items: Progressive Sequence, Codex Page, Assassin Seal, Progressive Hidden
  Blade, shop equipment, Florins, and traps (Templar Tax, Bad Medicine, Wanted).
- Region gating by story progression.
- Templar Grip (optional, off by default): reverse-notoriety challenge — your
  notoriety is clamped to a floor that only Progressive Templar Grip items can
  lower (25% per item). Community-suggested design; needs in-game calibration.
