# DW3 Field Editor

An editor for the fields of *Digimon World 2003* (the maps you walk around), for the
game's native PC port, built from [dw3_decomp](https://github.com/ReGame-Labs/dw3_decomp). It shows each field
as the game draws it, with the layers the game uses underneath: where you can walk,
where exits and events trigger, and where random battles start, and edits them: paint
those layers, move, add and remove characters, objects and the start, change exits,
dialogue lines and battles, then try the field in the game with one key.

**Nothing from the game is included.** The editor reads your own copy of the game,
through the native port's data folder.

## What it shows

- **Every field**, by the names the game gives them: a tree of areas (Asuka City,
  Central Sector...) and their places (Main Lobby, Central Park...), with a search.
- **The map**, as the game composes it: the background's 128-pixel tiles in their
  three depths, the animated objects, and the characters with their sprites, each
  facing its way.
- **The logic layers**, over the map:
  - **Floor**: where the player can walk (darker green), and the slopes (lighter);
  - **Triggers**: the cells that set off an exit, a climb, a warp or an event, a color
    per slot;
  - **Battle areas**: where random battles start, a color per area.
- **Exits**, marked where they are, with the field they lead to. A click on the
  destination opens it.
- **Characters**: when each one appears, and what it says in each case, with the
  game's text and the conditions in words ("flag 1A:1F is 1", "has" and the item's name).
  - A spot can hold several versions of a character, for the story's points: the map
    shows one sprite with their count (×32), and Properties steps through them. The
    game shows the first whose conditions hold.
  - Characters without a sprite are spots the player talks to (signs, terminals): a
    dashed ring.
  - Characters at 0, 0 are placed by the field's events, and listed apart.
- **Objects**: their sprite, depth and animation.

## Requirements

- Windows 10 or 11, 64-bit.
- The native port, set up with your disc, so its `data/` folder holds `files/`.
- The fields' tables, made once by the port:

      dw2003.exe --dump-stages data/stages

  This writes one file per field to `data/stages`, plus `characters.json`.
- A port that loads the editor's changes (`data/mods/stages`) and takes `--field`, for
  Test in game.

## Use

Run `dw3fe.exe`. The first time it asks for the port's `data` folder, and remembers it.

| Control | Does |
|---|---|
| Mouse wheel | zoom, around the cursor |
| Right or middle button drag, or Space + left drag | pan |
| Left click | select a character, an exit or an object |
| F | fit the map to the window |
| 1 | actual size |
| 2 to 9 | turn the layers on and off |

The window:

- **Fields** (left): the areas and their places. Two places with the same name (the
  Asuka and Amaterasu servers' copies, story copies) show their number.
- **The map** (middle), with the layer buttons and the overlays' opacity above it.
- **Contents** (right): the field's characters, exits and triggers, and objects. A
  click selects the item on the map and centers it.
- **Properties** (right): what is selected, or the field itself.
- **The status bar**: the position under the cursor, and what each layer holds there.
- **About** (on the menu bar): the version, license and credits.

## Editing

| Tool | Key | Does |
|---|---|---|
| Select | V | select; drag a character's spot (all its versions), an object or the start to move it |
| Paint floor | B | where the player can walk: blocked, walkable, or a slope the map already has |
| Paint triggers | T | the cells that set off a slot (an exit, a climb, an event...), and the way the player must face |
| Paint areas | A | where random battles start, and which of the four areas |

While painting, the brush's size is in the bar above the map; **Shift** paints a
rectangle and **Alt** picks the value under the cursor. A **right click** on the map
adds a character, a version of a spot, an object or a slot, duplicates or deletes the
selection, or moves the start there. **Delete** removes what is selected.

**Properties** edits what is selected: a character's look, place, facing, when it
appears, and each of its lines (when, which text, what it does after); an exit's type,
destination, arrival and conditions; an object's frame, depth and animation; the start;
and, from **Battles** in Contents, each area's rate and its eight battles. Conditions and
actions are written as `code=value` pairs with the code in hex (`1A1F=1`, the flag
1A:1F set; `602A=1`, story point 0x2A), and shown in words below.

**Story point** (above the map) shows each spot's version as at that point of the story,
and Test in game starts there.

**Ctrl+Z / Ctrl+Y** undo and redo, **Ctrl+S** saves, **F5** saves and tests in the game.
**File > Revert to the game's** deletes the field's saved changes.

### What saving writes

Only what differs from the game, in the port's `data/` folder:

- `mods/stages/<stage>.json`: the tables that changed (characters, objects, slots,
  start, battles), in the form `--dump-stages` writes. The port applies it after the
  field's own setup.
- `mods/<file>.bin`: the field's file with the painted logic maps in it. The port
  reads it instead of the game's.

A field as the game has it leaves no file. Some limits come from the game: a field has
at most 31 slots, and a logic map at most 256 different 64-pixel cells.

### Command line

    dw3fe [wstag330]                   opens the editor (and a field)
    dw3fe --data DIR                   the port's data folder
    dw3fe --shot out.png wstag330      draws a field's map to a picture and exits
    dw3fe --check                      reads every field and prints what it found
    dw3fe --selftest                   checks what saving rests on, over every field:
                                       each logic map encoded and read back the same
    dw3fe wstag245 --select actor:19   opens a field with something selected
                                       (actor:N, slot:N, object:N, start)

## Building from source

1. Install [MSYS2](https://www.msys2.org) (or `winget install MSYS2.MSYS2`).
2. In an MSYS2 terminal, install the UCRT64 compiler, Qt 6, CMake and Ninja:

       pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-qt6-base \
                 mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja

3. Install [Python 3](https://www.python.org), and optionally Pillow
   (`pip install pillow`), which makes the exe's icon in every size Windows uses. Then
   build:

       python build.py

   This writes `build/dw3fe.exe` with every DLL it needs next to it, so the folder runs
   on its own. `python build.py --clean` builds from scratch. MSYS2 is found at
   `C:\msys64`, or wherever `MSYS2_DIR` says.

### Making a release

    python tools/package.py v0.1.0

builds, then packs `build/dw3-field-editor-v0.1.0-win64.zip`: the exe, its DLLs, this
README, the license and credits, and `licenses/` with the license of every DLL, found
through the MSYS2 package it comes from. It then unpacks the zip to a temporary folder
and checks that it runs on its own and reads every field.

### Troubleshooting

- **"no g++.exe in C:\msys64\ucrt64\bin"**: the packages of step 2 are missing, or
  MSYS2 is somewhere else (set `MSYS2_DIR`).
- **"build/dw3fe.exe is open"**: close the editor and build again.
- **"No stages there"**: the data folder has no `stages/`. Run
  `dw2003.exe --dump-stages data/stages` with the port.

## How it reads a field

- **The stage's tables** (its objects, triggers, characters, battles and files) are
  the game's own C, compiled into the port. The port runs each field's setup and
  writes what it set (`--dump-stages`).
- **The background** is a file of 128-pixel tiles, each RLEN-packed: up to five
  sprites for each of three depths, then an 8-bit image.
- **The logic maps** are a tree of cells, 128 down to 8 pixels, ending in a byte per
  pixel: the floor's slope, a trigger's slot and facing, or a battle area.
- **The objects** are frames of the field's sprite sheet, drawn from its images in an
  emulated VRAM, as the game's sprite drawer does.
- **The characters** are the first standing frame of their animation file.
- **The text** is the game's English text files, through the font's glyph table.

## Not there yet

- **Bigger fields and new ones**: growing the grid, importing a background from a
  picture, making a new field.
- **Event scripts**: their names show, not what they do.
- **Text in other languages** than English, and writing new texts: a line picks one of
  the field's texts.

## License

MIT, see [LICENSE](LICENSE). Credits for the work it builds on: [CREDITS.md](CREDITS.md).
