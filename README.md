# DW3 Field Editor

An editor for the fields of *Digimon World 2003* (the maps you walk around), built on
[dw3_decomp](https://github.com/ReGame-Labs/dw3_decomp). It reads them from your disc and shows each field
as the game draws it, with the layers the game uses underneath: where you can walk,
where exits and events trigger, and where random battles start, and edits them: paint
those layers, move, add and remove characters, objects and the start, change exits,
dialogue lines and battles.

Your changes are played in the game's native PC port, which isn't released yet; with
it, Test in game tries the field with one key.

**Nothing from the game is included.** The editor reads your own copy of the game:
your disc image, or the native port's data folder. Not affiliated with or endorsed by
Bandai or Bandai Namco; *Digimon* is their trademark.

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
  game's text and the conditions in words ("the story is at 0x05 (after Seiryu Leader,
  <field>)", "Agumon is in the party", "has" and the item's name).
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
- Your disc of *Digimon World 2003* (Europe, SLES-03936) as an image: `.cue`/`.bin`
  or `.iso`. The editor checks it's that one.
- To play your changes, and for Test in game: the native port (not released yet), which
  loads them from its data folder (`data/mods`), its new fields and events, and takes
  `--field` and `--event`.

Without the port, the editor reads the fields from the disc: it runs each field's setup
code from the disc as the game does, and writes what it set to the folder you pick
(`stages/`, once), where your changes go too (`mods/`). With the port, pick its data
folder instead, with `files/` and the fields' tables the port makes:

    dw2003.exe --dump-stages data/stages

## Use

Download the zip from the [releases](https://github.com/Feerdus95/dw3-field-editor/releases), unzip it anywhere and
run `dw3fe.exe`. The first time it asks for your disc image and a folder for its work,
or for the port's `data` folder, and remembers them.

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
selection, adds an object from a picture or an event, or moves the start there; the
**Add** menu adds the same in the middle of what the map shows. **Delete** removes what
is selected.

**Properties** edits what is selected: a character's look, place, facing, when it
appears, and each of its lines (when, which text, what it does after); an exit's type,
destination, arrival and conditions; an object's frame, depth and animation; the start;
and, from **Battles** in Contents, each area's rate and its eight battles.

Conditions and actions are a row each: what kind (the story's point or span, a flag, a
field visited, an item, a card, the partners, money...), which one, picked from a list
in the game's names, and whether it holds or not (or, as an action, set or clear, give
or take). **Code (advanced)** keeps any other as the game's tables have it, in hex; a
row's tooltip shows its code too.

The story's points are named after the event that moves the story on to each, and where
(0x05: after Seiryu Leader, and the field where). Flags are named by their group and number
(1A:1F) and by what sets them; **Story > Flags** lists every flag the fields and the
game's code use, with each use, lets you name them (kept in mods/flags.json) and makes
**new flags** nothing in the game uses, for a story of your own.

**Story point** (above the map) shows each spot's version as at that point of the story
(the spans included), and Test in game starts there.

**Ctrl+Z / Ctrl+Y** undo and redo, **Ctrl+S** saves, **F5** saves and tests in the game;
with an event selected, the game plays it once the field is up, and with a character, an
object or a slot selected, the player starts beside it.

The status bar shows the **sprite memory** the field leaves: the share of the VRAM its
characters and objects share that is taken, how many more characters would get an image
slot of their own and how many new pages of object images would go. Busy fields (the
towns) have little or none left.

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
  depth. A picture of another size can resize the map to fit it, or be fitted to the map
  (scaled to cover it, its middle kept).

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
language as written; switch the language to translate it (until then Properties says it
is the same in every language). The fields share their text
files: a change to a text shows wherever it is said, and the dialog tells how many lines
say it.

### Events

An event is a script the game runs: characters walk, pose and talk, the camera moves,
the script waits for them, and it can end by leaving for another field. Events (in
Contents) shows them; **Properties** edits the script, a command at a time: change one,
move it up or down, remove it, or add one at the end. Places, walks and camera moves are
marked on the map, numbered: drag a mark to move it. A **Say** shows its text, which can be edited or written
anew as a character's line can. Some events are code the game runs: those show what
starts them, without a script.

**Right click > Add an event** makes one, numbered from 9000, its first command a walk to
where you clicked. Something has to start it: a slot of type **Event** (Properties picks
the event; paint its trigger cells), or a character's line, with the action **Start an
event** and the event. With an event selected, **F5** plays
it in the game.

### Objects of your own

**Right click > Add an object from a picture...** makes an object from a picture: one
still frame, or the frames of an animation side by side, which the object plays in a
loop; it shows the frames as they'll be cut, each one's anchor and the colors first. The picture gets 15 colors of its own and is anchored at its bottom middle, where
you clicked. It goes into a page of the field's object images that the editor made, or a
new one: a field has room for three pages, so the fields that use all three take no
more.

### Characters of your own

A new character is drawn to the game's own measure, as its people are: a 4-bit sprite
(15 colors and a transparent one), standing in 4 frames and walking in 6,
seen from 5 directions (south, south-west, west, north-west, north; the game mirrors the
others).

Characters are picked by their look: a grid of each one's picture, named after who says
its lines in the game (or the name you gave it), searchable by name, field or number, this
field's first. The Character of a character's Properties, Add a character and Export a
template all use it.

- **Characters > Export a template...** writes a character as a sheet to draw over: a
  `.png` with its layout inside (a row per animation and direction, a cell per drawing,
  the times and the order the game plays them in). **Blank template...** writes the
  standard one, empty, with faint guides. If the program you draw in drops the layout
  when saving, the import asks which character the sheet came from.
- Draw over the sheet, keeping its cells, and **Characters > Import a character...**
  reads it back. It says what isn't to the template yet (a cell's size, an empty cell,
  more than 15 colors) instead of guessing; empty cells can be filled with the drawing
  before, as an exception you choose. It asks for a name and offers to place it on the
  field open.
- **Characters > Your characters...** lists them: place one on the field, import new
  drawings for it (every field that has it shows them), export its template, rename or
  delete it.
- A character added to a field asks for its first line, its name as the speaker. (The
  text files' first text is empty: the game only ends its lists of lines with it, and
  saying it crashes the game.)
- The characters on a field at once each need an image slot, where their frames go in
  VRAM; the game gives each look its own, shared only by characters of that look. The
  editor does the same: a character added or changed takes a slot of its look, or a new
  one where the field's VRAM has room. A slot shared by two looks shows in red, with a
  button to give it its own.

The port finds the new characters in `mods/characters.json`.

### New fields

**File > New field** makes a field of your own from the one open: a copy of it, or an
empty map of the size you choose, walkable all over. It takes a mode no field has
(0x2F0 on) and the banner's area and place names: the game's, or new ones you type, which
go to every language's names. It runs the
code of the field it came from: its sprites, music and texts, and its events, which check
the story as they do there. To reach it in the game, **Reach it from** leads one of the
open field's exits to it as it is made, or lead an exit of any field to it later (it is
in the exits' list); Test in game opens it directly. A new field has no "visited" flag
(the game has one for the modes up to 0x2EF only). **File > Delete this new
field** removes it and its files.

### What saving writes

Only what differs from the game, in the data folder (the port's, or the editor's own):

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
- `mods/flags.json`: the names you gave the flags (Story > Flags), for the editor only.

A field as the game has it leaves no file. Some limits come from the game: a field has
at most 31 slots, a logic map at most 256 different 64-pixel cells, and a map at most 255
tiles.

### Command line

    dw3fe [wstag330]                   opens the editor (and a field)
    dw3fe --data DIR                   the port's data folder, or the editor's own
    dw3fe --disc game.cue              the disc: the fields and files from it, when the
                                       data folder has none
    dw3fe --dump-disc game.cue DIR     writes the fields' tables from the disc to DIR,
                                       as the port's --dump-stages does
    dw3fe --shot out.png wstag330      draws a field's map to a picture and exits
    dw3fe --check                      reads every field and prints what it found,
                                       and the conditions and actions left as numbers
    dw3fe --selftest                   checks what saving rests on, over every field:
                                       each logic map, background tile, text and event
                                       script written and read back, the maps resized
                                       back and forth, a sprite imported
    dw3fe wstag245 --select actor:19   opens a field with something selected
                                       (actor:N, slot:N, object:N, event:N, start)
         [--window-shot out.png]       and saves the window and its properties
                                       (out-props.png) as drawn, then exits
    dw3fe --new-field wstag330 [--size 8x6] [--background picture.png]
                                       makes a new field from one: a copy, or an
                                       empty map of 8 x 6 tiles, its background a
                                       picture of the map's size
    dw3fe wstag330 --add-object picture.png 960,1000 [--frames 3]
                                       adds an object from a picture at a point of a
                                       field, and saves
    dw3fe --export-template 1B sheet.png
                                       a character's template (its number in hex, or
                                       "standard" for the blank one), its layout inside
    dw3fe --import-character sheet.png a filled template as a character of your own
    dw3fe wstag330 --add-character 196 960,990
                                       puts a character (hex) at a point of a field
                                       with an image slot and the line "...", and saves

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

### The game's codes

`src/core/game_codes.inc`, the story's steps (the event that moves the story on to each
point) and the flags the game's code sets and checks, is written from dw3_decomp's C, and
`src/core/stage_setups.inc`, where each field's setup starts, from its symbols:

    python tools/game_codes.py [path/to/dw3_decomp]

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
- **"No fields there"**: the data folder has no `stages/`. Start again and pick your
  disc image, or run `dw2003.exe --dump-stages data/stages` with the port.

## How it reads a field

- **The stage's tables** (its objects, triggers, characters, battles and files) are
  set by each field's setup code, in its overlay (`AAA/PRO/WSTAGnnn.PRO`). The editor
  loads the executable, FIELDSTG and the overlay where the game does, runs the setup on
  a small MIPS interpreter and reads the tables it set from that memory; the port does
  the same natively (`--dump-stages`), and both write the same.
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
- **The events** are the scripts FIELDSTG's event task reads, words of commands, read
  with the tables.
- **The objects' pictures** are 4-bit pages of 256 x 256 pixels loaded at VRAM's
  (0x140, 0x100), with the palettes inside the page, as the game's own.

## Not there yet

- **Playing your changes** needs the native port, not released yet. The editor doesn't
  write them back to a disc image for the original game or an emulator.
- **The story's events**: the game starts some events at a point of the story from a
  list of its own; a field's new events start from a slot or a line.

## License

MIT, see [LICENSE](LICENSE). Credits for the work it builds on: [CREDITS.md](CREDITS.md).
