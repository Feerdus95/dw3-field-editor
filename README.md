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
- **Events**: each one's script, the commands in words (a character walks there, says
  this, the camera goes there...), with where it takes characters marked on the map,
  and what starts it.
- **The texts** in English, French, Italian, German or Spanish (View > Text language).

## Requirements

- Windows 10 or 11, 64-bit.
- The native port, set up with your disc, so its `data/` folder holds `files/`.
- The fields' tables, made once by the port:

      dw2003.exe --dump-stages data/stages

  This writes one file per field to `data/stages`, plus `characters.json`. Dump them
  again after updating the port: the events' scripts come from there.
- A port that loads the editor's changes (`data/mods/stages`), its new fields and events,
  and takes `--field` and `--event`, for Test in game.

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
- **The tools** (top): select, the three paint tools, undo and redo, and Test in game;
  while painting, a bar below with what to paint and the brush's size (see [Editing](#editing)).
- **The map** (middle), with the layer buttons and the overlays' opacity above it, and
  the story point the characters show.
- **Contents** (right): the field's characters, exits and triggers, objects and events.
  A click selects the item on the map and centers it.
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
selection, adds an object from a picture or an event, or moves the start there.
**Delete** removes what is selected.

**Properties** edits what is selected: a character's look, place, facing, when it
appears, and each of its lines (when, which text, what it does after); an exit's type,
destination, arrival and conditions; an object's frame, depth and animation; the start;
and, from **Battles** in Contents, each area's rate and its eight battles. Conditions and
actions are written as `code=value` pairs with the code in hex (`1A1F=1`, the flag
1A:1F set; `602A=1`, story point 0x2A), and shown in words below.

**Story point** (above the map) shows each spot's version as at that point of the story,
and Test in game starts there.

**Ctrl+Z / Ctrl+Y** undo and redo, **Ctrl+S** saves, **F5** saves and tests in the game;
with an event selected, the game plays it once the field is up.
**File > Revert to the game's** deletes the field's saved changes.

### Bigger maps and new backgrounds

- **Map > Resize** adds tiles of 128 pixels on any side, or takes them away. What stands
  on the map (characters, objects, the start) moves with its content, and so do the
  exits of every field that arrive here (they are saved with the field). New cells are
  blocked, without triggers or battles: paint them. A map has at most 255 tiles.
- **Map > Export background** writes the background's three depths as pictures:
  `name-back.png` (the ground, behind everything), `name-middle.png` (behind the
  characters, unless a depth trigger puts the player behind it) and `name-front.png`
  (over the characters: roofs, treetops).
- **Map > Import background** reads them back: pick any of the three, and the editor
  reads the others next to it. Only the tiles that changed are made again, each with its
  own 256 colors. A picture with another name is the whole background, on the middle
  depth. A picture of another size can resize the map to fit it.

A field whose background changed gets a file of its own: the game shares backgrounds
between a place's story copies, and those stay as they are.

### Texts

A character's line shows its text; **Edit the text...** changes it, and **New text...**
writes one of its own for the line. The dialog has the speaker's name and the text: a line
break is a new line, an empty line a new page (the talk box shows three lines a page),
`{player}` the player's name, and other `{..}` are codes the game's texts have, kept as
they are. It says when a line is wider than the field's texts get, or a page longer than
three lines. The letters are the font's: the European ones (é, ñ, ü, ß...) too.

The texts are in the language View > Text language shows. A new text goes to every
language as written; switch the language to translate it. The fields share their text
files: a change to a text shows wherever it is said, and the dialog tells how many lines
say it.

### Events

An event is a script the game runs: characters walk, pose and talk, the camera moves,
the script waits for them, and it can end by leaving for another field. Events (in
Contents) shows them; **Properties** edits the script, a command at a time: change one,
move it up or down, remove it, or add one at the end. Places, walks and camera moves are
marked on the map, numbered. A **Say** shows its text, which can be edited or written
anew as a character's line can. Some events are code the game runs: those show what
starts them, without a script.

**Right click > Add an event** makes one, numbered from 9000, its first command a walk to
where you clicked. Something has to start it: a slot of type **Event** (Properties picks
the event; paint its trigger cells), or a character's line, with the action
`9100=1` for event 9000, `9101=1` for 9001 and so on. With an event selected, **F5** plays
it in the game.

### Objects of your own

**Right click > Add an object from a picture...** makes an object from a picture: one
still frame, or the frames of an animation side by side, which the object plays in a
loop. The picture gets 15 colors of its own and is anchored at its bottom middle, where
you clicked. It goes into a page of the field's object images that the editor made, or a
new one: a field has room for three pages, so the fields that use all three take no
more.

### Characters of your own

A new character is drawn to the game's own measure, as its people are: a 4-bit sprite
(15 colors and a transparent one), standing in 4 frames and walking in 6,
seen from 5 directions (south, south-west, west, north-west, north; the game mirrors the
others).

- **Characters > Export a template...** writes one of the game's characters as a sheet
  (`.png`) and its layout (`.json`, beside it): a row per animation and direction, a
  cell per drawing, with the times and the order the game plays them in. **Blank
  template...** writes the standard one, empty, with faint guides.
- Draw over the sheet, keeping its cells, and **Characters > Import a character...**
  reads it back. It says what isn't to the template yet (a cell's size, an empty cell,
  more than 15 colors) instead of guessing; empty cells can be filled with the drawing
  before, as an exception you choose.
- The character gets a number of its own (406 on) and, in a character's Properties,
  can be picked like the game's. The characters on a field at once each need an image
  slot: **A slot of its own** (beside Image slot) makes one where the field's VRAM has
  room, as big as the character's frames. Right click on the map shares the field's
  first character's.

The port finds the new characters in `mods/characters.json`.

### New fields

**File > New field** makes a field of your own from the one open: a copy of it, or an
empty map of the size you choose, walkable all over. It takes a mode no field has
(0x2F0 on) and the banner's area and place names: the game's, or new ones you type, which
go to every language's names. It runs the
code of the field it came from: its sprites, music and texts, and its events, which check
the story as they do there. To reach it in the game, lead an exit of another field to it
(it is in the exits' list); Test in game opens it directly. **File > Delete this new
field** removes it and its files.

### What saving writes

Only what differs from the game, in the port's `data/` folder:

- `mods/stages/<stage>.json`: the tables that changed (characters, objects, slots,
  start, battles), in the form `--dump-stages` writes. The port applies it after the
  field's own setup.
- `mods/<file>.bin`: the field's file with the painted logic maps in it. The port
  reads it instead of the game's.

- `mods/<3000 + the field's number>.bin`: its background, once changed.
- `mods/<5000 + the field's number>.bin`: its objects' images, once a picture is added
  (6000 + its number for a new field).
- `mods/<text file>.bin`: the text files whose texts changed, in each language.
- `mods/<7000 + its number - 406>.bin`: a character of your own, its frames and
  animations, and `mods/characters.json`, the list of them.
- A new field: `mods/stages/new<mode>.json`, the whole of its tables, and its two files,
  `mods/<4000 + 2 × its number>.bin` (the background) and the next one (its logic maps).

A field as the game has it leaves no file. Some limits come from the game: a field has
at most 31 slots, a logic map at most 256 different 64-pixel cells, and a map at most 255
tiles.

### Command line

    dw3fe [wstag330]                   opens the editor (and a field)
    dw3fe --data DIR                   the port's data folder
    dw3fe --shot out.png wstag330      draws a field's map to a picture and exits
    dw3fe --check                      reads every field and prints what it found
    dw3fe --selftest                   checks what saving rests on, over every field:
                                       each logic map, background tile, text and event
                                       script written and read back, the maps resized
                                       back and forth, a sprite imported
    dw3fe wstag245 --select actor:19   opens a field with something selected
                                       (actor:N, slot:N, object:N, event:N, start)
    dw3fe --new-field wstag330 [--size 8x6] [--background picture.png]
                                       makes a new field from one: a copy, or an
                                       empty map of 8 x 6 tiles, its background a
                                       picture of the map's size
    dw3fe wstag330 --add-object picture.png 960,1000 [--frames 3]
                                       adds an object from a picture at a point of a
                                       field, and saves
    dw3fe --export-template 1B sheet.png
                                       a character's template (its number in hex, or
                                       "standard" for the blank one) and its .json
    dw3fe --import-character sheet.png a filled template as a character of your own
    dw3fe wstag330 --add-character 196 960,990
                                       puts a character (hex) at a point of a field
                                       with an image slot of its own, and saves

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
  sprites for each of three depths, then an 8-bit image of at most 256 x 128 pixels with
  its 256 colors. Writing one, the editor drops what a nearer depth hides, cuts each depth
  in strips and packs them in that image.
- **The logic maps** are a tree of cells, 128 down to 8 pixels, ending in a byte per
  pixel: the floor's slope, a trigger's slot and facing, or a battle area.
- **The objects** are frames of the field's sprite sheet, drawn from its images in an
  emulated VRAM, as the game's sprite drawer does.
- **The characters** are the first standing frame of their animation file.
- **The text** is the game's text files, a copy for each language, through the font's
  glyphs and icons (the European letters are icons 59 on).
- **The events** are the scripts FIELDSTG's event task reads, words of commands the port
  dumps with the tables.
- **The objects' pictures** are 4-bit pages of 256 x 256 pixels loaded at VRAM's
  (0x140, 0x100), with the palettes inside the page, as the game's own.

## Not there yet

- **The story's events**: the game starts some events at a point of the story from a
  list of its own; a field's new events start from a slot or a line.

## License

MIT, see [LICENSE](LICENSE). Credits for the work it builds on: [CREDITS.md](CREDITS.md).
