# Changelog

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
