# AC2AP — Install & Play (alpha)

A multiworld randomizer for **Assassin's Creed II** on the
[Archipelago](https://archipelago.gg) framework. This package is **pre-compiled** —
you do NOT need to build anything.

## What you need
- **Assassin's Creed II** for PC (build 1.01, 32-bit `AssassinsCreedIIGame.exe`).
- **An existing save on slot 1.** If this is a fresh install, play the game normally
  (without AC2AP) at least until the **first autosave** — that creates the `1.save`
  file the client watches. No save file yet = a red "SAVE NOT FOUND" toast.
  - Play on **save slot 1**: the client watches `1.save`. On slot 2/3 it never sees
    your actions (set `save_path` to `2.save`/`3.save` instead, or use slot 1).
- **⚠️ Ubisoft Connect: turn OFF cloud save sync** (Ubisoft Connect → menu →
  Settings → General → uncheck "Enable cloud save synchronisation"), on ANY version.
  If it's on, Ubisoft overwrites your local save with the cloud copy at launch — it
  reverts your progress and the client stops seeing your checks. Disable it first.
- An **ASI loader** so the game loads `.asi` mods — e.g.
  [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases):
  download `dinput8.dll` (or `winmm.dll`) and drop it next to the game `.exe`.
- **Archipelago** 0.6.7+ ([download](https://github.com/ArchipelagoMW/Archipelago/releases)).

## 1. Install the world (once)
Put **`ac2.apworld`** into your Archipelago `custom_worlds/` folder.
Then generate a game (or join one someone hosts) with an `ac2` YAML — the
Archipelago launcher's "Generate Template Options" gives you a starting YAML for
"Assassin's Creed II".

## 2. Install the client (once)
Copy these three files into the game's ASI folder (the folder the loader reads —
usually a `scripts\` folder next to the `.exe`, or next to the loader):
- `AC2AP.asi`
- `AC2AP.ini`
- `AC2AP_map.txt`

The connection is done in-game (F8), so you don't edit the ini for that. The save
path **auto-detects Ubisoft Connect and Skidrow** installs (slot `1.save`) — a green
**"Save found"** toast confirms it in-game.

If it can't find your save (red toast, or check `AC2AP.log`), set `save_path=` in
`AC2AP.ini` to the **full path of the `1.save` file itself** — not the folder that
contains it (no quotes needed, spaces are fine), e.g.
`save_path=C:\Program Files (x86)\Ubisoft\Ubisoft Game Launcher\savegames\1234567\4\1.save`.
Note: with several Ubisoft profiles it uses the **first** one that has a save — set
`save_path` if that's the wrong account.

## 3. Play
1. Launch the game and load a save.
2. Wait a few seconds — a blue **"AC2AP overlay ready"** message appears top-right.
3. Press **F8** (or **INSERT**) to open the connection menu.
4. Type your **server** (`host:port`), **slot** (your player name), and **password**
   if any, then click **Connect**. (Ezio stays still while the menu is open.)
5. A green **"Connected as ... !"** message confirms it. Play — completing checks
   sends items to the multiworld, and received items pop up as toasts.

## Notes
- This is an early **alpha**. See the project's README / Known Issues for what's
  enabled and what's experimental (e.g. Templar Grip).
- If nothing loads: make sure the ASI loader is installed and the three client
  files are in the folder the loader actually scans.
- Assassin's Creed II is a trademark of Ubisoft. This is an unofficial fan mod,
  not affiliated with Ubisoft, and ships no game assets.
