# AC2AP — Assassin's Creed II × Archipelago

A multiworld randomizer for **Assassin's Creed II** (PC), built for the
[Archipelago](https://archipelago.gg) framework.

> ⚠️ **Alpha-alpha.** This is an early, rough release meant for testing and
> feedback, not a finished randomizer. Expect missing features and sharp edges.
> The guiding rule of the project is *stability first* — a feature ships only
> once it has been verified in-game — so what is enabled here is deliberately a
> small, tested subset.

## What it is

AC2AP has two parts:

- **`apworld/ac2/`** — the Python world for Archipelago. Defines the items,
  locations, region logic and player options (`.yaml`) used by the AP
  generator.
- **`client/`** — an ASI mod (C++) injected into the game. It embeds an
  Archipelago client (via [apclientpp](https://github.com/black-sliver/apclientpp)),
  watches your save for completed checks, sends them to the server, and applies
  received items in-game.

## Status — what works today

| Feature | State |
|---|---|
| Main story missions (Sequences 1–14) as checks | ✅ |
| Treasure chests & feathers (per-district, and per-item "sanity" modes) | ✅ |
| Monteriggioni statues | ✅ |
| Shop purchases (blacksmith / tailor / art dealer) | ✅ |
| Codex pages | ✅ |
| Florins + trap items (Templar Tax, Bad Medicine, Wanted) | ✅ |
| Region gating by story progression | ✅ |
| DeathLink | ✅ |
| Viewpoints, glyphs, tombs, secondary missions | 🚧 not yet reliable / disabled |
| Villa renovations | 🚧 in progress |

Known issues and limitations are tracked internally; this alpha intentionally
disables anything not yet proven stable.

## Requirements

- Assassin's Creed II for PC (build **1.01**, 32-bit `AssassinsCreedIIGame.exe`).
- An [Ultimate ASI Loader](https://github.com/Thirisc/Ultimate-ASI-Loader) (or
  equivalent, e.g. a `winmm.dll`/`dinput8.dll` proxy) so the game loads `.asi` mods.
- [Archipelago](https://github.com/ArchipelagoMW/Archipelago/releases) 0.6.7+ to
  generate a seed and run the server.

## Install (playing)

Grab `ac2.apworld` and the client files (`AC2AP.asi`, `AC2AP.ini`,
`AC2AP_map.txt`) from the GitHub **Releases** page — the built `.asi` is not in
the source tree.

1. Drop `ac2.apworld` into your Archipelago `custom_worlds/` folder, then
   generate a game as usual with an `ac2` YAML.
2. Copy the client files into the game's ASI scripts folder (next to the loader):
   - `AC2AP.asi`
   - `AC2AP.ini`
   - `AC2AP_map.txt`
3. Edit `AC2AP.ini`:
   ```ini
   [ac2ap]
   save_path=          ; blank = auto-detect the Skidrow default
   server=archipelago.gg:38281
   slot=YourSlotName
   password=
   enable_health_hook=1
   ```
4. Launch the game. The client logs to `AC2AP.log` next to the `.asi`.

## Build (the ASI)

The client is 32-bit and built with MSVC + CMake. Third-party dependencies are
**not** committed — populate `client/deps/` with the pinned libraries first
(repos and commits are listed in [client/README.md](client/README.md)).

```sh
cd client
cmake -B build -A Win32 -DAC2AP_WITH_AP=ON
cmake --build build --config Release
```

The result is `AC2AP.asi`. Package the `.apworld` by zipping `apworld/ac2/` into
`ac2.apworld` (a zip whose top-level folder is `ac2/`).

## Layout

```
apworld/ac2/      Archipelago world (Python)
client/src/       ASI mod source (C++)
client/deps/      vendored third-party headers
client/dist/      shipped client files (.ini template, generated map)
client/gen_map.py dev tool that regenerates AC2AP_map.txt
```

## Disclaimer

Assassin's Creed II is a trademark of Ubisoft Entertainment. This is an
unofficial, fan-made mod and is **not affiliated with or endorsed by Ubisoft**.
It ships no game assets — you must own a copy of the game. See [LICENSE](LICENSE).
