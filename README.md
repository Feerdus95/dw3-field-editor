# DW3 Field Editor

An editor for the maps of *Digimon World 2003* (PlayStation, 2002). In the game these
maps are called **fields**: the towns, rooms and dungeons you walk around in.

The editor opens each field as the game draws it. On top of the picture, it shows the
layers the game uses but never shows you: where you can walk, where exits and events
start, and where random battles happen.

What you can do with it today:

- Look at any field of the game, with its characters, their lines and when they appear.
- Change a field: paint where you can walk, move or add characters and objects, change
  exits, dialogue, battles and the scripted scenes (events).
- Make things of your own: new characters, new objects from pictures, new fields.

**To play your changes you need the game's native PC port**, a separate project that
isn't released yet (see [The native port](#the-native-port)). Until then you can explore
and edit fields and save your changes, but not play them.

**Nothing from the game is included.** The editor reads your own copy of the game. Not
affiliated with or endorsed by Bandai or Bandai Namco; *Digimon* is their trademark.

## Getting started

You need:

- **Windows 10 or 11, 64-bit.**
- **Your own disc of the European version of the game**, *Digimon World 2003*
  (SLES-03936), as a disc image: a `.bin` + `.cue` pair, or an `.iso`. Make it from your
  disc with any CD ripping program that saves raw `.bin`/`.cue` images. The American
  version (*Digimon World 3*, SLUS-01436) and the Japanese one don't work: the editor
  checks the disc and tells you if it's another.

Then:

1. Download the zip from the [releases page](https://github.com/Feerdus95/dw3-field-editor/releases) and unzip it anywhere.
2. Run `dw3fe.exe`. It asks where the game's fields are: choose **Your disc image...**
   and pick your `.cue`, `.bin` or `.iso`.
3. Pick an empty folder for the editor's work. The editor reads the fields from the disc
   into it once (a few seconds), and saves your changes there too.

Next time, the editor opens straight away: it remembers the disc and the folder.

## Words used here

- **Field**: a map you walk around in. In the game's files each field is a stage,
  numbered `wstag200` to `wstag974`; the Fields list shows that number when you hover a
  field, and the command line uses it (`wstag330`).
- **Mode**: the number the game uses to go to a field. Some fields have several (one
  per point of the story).
- **Slot**: an entry of a field's list of exits and triggers: an exit to another field,
  a climb, a warp, or the start of an event. Which cells of the map set it off is painted
  in the **Triggers** layer.
- **Story point**: how far the story is, a number from 0x00 to 0x2D. Characters and exits
  change with it. The editor names each point after what moves the story on to it.
- **Flag**: a yes/no the game remembers (a chest opened, a talk done). Characters, lines
  and exits can check flags; lines can set them.
- **Event**: a scripted scene: characters walk and talk, the camera moves.
- **Sprite memory**: the room a field has in the PlayStation's video memory for its
  characters' and objects' pictures. Busy fields have little left.
- Numbers written like `0x2F0` are in hexadecimal, as the game's data has them.

## What it shows

- **Every field**, by the names the game gives them: a tree of areas (Asuka City,
  Central Sector...) and their places (Main Lobby, Central Park...), with a search.
- **The map**, as the game composes it: the background in its three depths, the
  animated objects, and the characters with their sprites, each facing its way.
- **The logic layers**, over the map:
  - **Floor**: where the player can walk (darker green), and the slopes (lighter).
  - **Triggers**: the cells that set off an exit, a climb, a warp or an event, a color
    per slot.
  - **Battle areas**: where random battles start, a color per area.
- **Exits**, marked where they are, with the field they lead to. A click on the
  destination opens it.
- **Characters**: when each one appears and what it says, with the game's text and the
  conditions in words ("the story is at 0x05 (after Seiryu Leader, Seiryu City)",
  "Agumon is in the party", "has" and the item's name).
  - One spot can hold several versions of a character, one for each part of the story.
    The map shows one sprite with their count (×32), and Properties steps through them.
    The game shows the first version whose conditions hold.
  - Characters without a sprite are spots the player talks to (signs, terminals). They
    show as a dashed ring.
  - Characters at 0, 0 are placed by the field's events, and are listed apart.
- **Objects**: their sprite, depth and animation.
- **Events**: each one's script, its commands in words (a character walks there, says
  this, the camera goes there...), with the places it takes characters marked on the
  map, and what starts it.
- **The texts** in English, French, Italian, German or Spanish (View > Text language).

## Using it

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
- **The tools** (top): select, the three paint tools, undo and redo, and Test in game
  (with the port). While painting, a bar below shows what to paint and the brush's size.
- **The map** (middle), with the layer buttons and the overlays' opacity above it, and
  the **Story point** the characters are shown at.
- **Contents** (right): the field's characters, exits and triggers, objects and events.
  A click selects the item on the map and centers it.
- **Properties** (right): what is selected, or the field itself.
- **The status bar**: the position under the cursor, what each layer holds there, and
  the sprite memory the field has left.
- **About** (on the menu bar): the version, license and credits.

## Editing

| Tool | Key | Does |
|---|---|---|
| Select | V | select; drag a character's spot (all its versions), an object or the start to move it |
| Paint floor | B | where the player can walk: blocked, walkable, or a slope the map already has |
| Paint triggers | T | the cells that set off a slot (an exit, a climb, an event...), and the way the player must face |
| Paint areas | A | where random battles start, and which of the four areas |

- While painting, the brush's size is in the bar above the map. **Shift** paints a
  rectangle and **Alt** picks the value under the cursor.
- A **right click** on the map adds things there: a character, a version of a spot, an
  object, a slot, an object from a picture, an event. It also duplicates or deletes the
  selection, or moves the player's start there. The **Add** menu does the same in the
  middle of the view.
- **Delete** removes what is selected.
- **Ctrl+Z / Ctrl+Y** undo and redo, **Ctrl+S** saves.
- **File > Revert to the game's** deletes the field's saved changes.

**Properties** edits what is selected:

- a character: its look, place, facing, when it appears, and each of its lines (when
  it's said, which text, what happens after);
- an exit: its type, where it leads, where the player arrives, and its conditions;
- an object: its frame, depth and animation;
- the player's start;
- the battles (**Battles** in Contents), a card per battle area painted on the map, in
  its color: how often battles come there (very rarely, about every 256 steps, to very
  often, about every 43), the battlefield and the music (each named by the fields that
  have it), and **who appears**: the Digimon you meet, by name and level, and how often
  (3 in 8). **Change...** picks others from the game's encounters, searched by a
  Digimon's or a field's name; **+**, **-** and **×** meet them more often, less often or
  never; **Add someone to meet...** adds more. Area 4 also holds the field's event
  battles, which events start by number (Battle 1 to 8). Areas not painted are folded
  away, and **Show the numbers** shows each battle as the game keeps it.

**Conditions and actions** are a row each: what kind of thing (the story point, a flag,
a field visited, an item, a card, the partners, money...), which one, picked from a list
by the game's names, and whether it must hold or not (or, as an action, set or clear,
give or take). **Code (advanced)** keeps any other as the game's tables have it, in
hexadecimal; a row's tooltip shows its code too.

**Story points** are named after the event that moves the story on to each, and where
it happens (0x05: after Seiryu Leader, in Seiryu City). **Flags** are named by their
group and number (1A:1F) and by what sets them. **Story > Flags** lists every flag the
fields and the game use, with each use. There you can name them and make **new flags**
that nothing in the game uses, for a story of your own.

**Story point** (above the map) shows each spot's version as at that point of the story.

The status bar shows the **sprite memory** the field has left: how much is taken, how
many more characters would get room of their own, and how many new pages of object
pictures would fit. Busy fields (the towns) have little or none left.

### Bigger maps and new backgrounds

- **Map > Resize** adds or removes 128-pixel tiles on any side. What stands on the map
  (characters, objects, the start) moves with it, and so do the exits of other fields
  that arrive here. New cells are blocked, without triggers or battles: paint them. A
  map has at most 255 tiles.
- **Map > Export background** saves the background's three depths as pictures:
  `name-back.png` (the ground, behind everything), `name-middle.png` (behind the
  characters) and `name-front.png` (over the characters: roofs, treetops).
- **Map > Import background** reads them back: pick any of the three, and the editor
  finds the others next to it. A picture with another name becomes the whole background,
  on the middle depth. A picture of another size can resize the map to fit it, or be
  fitted to the map.

A field whose background changed gets a file of its own: the game shares backgrounds
between copies of a place, and those stay as they are.

### Texts

A character's line shows its text. **Edit the text...** changes it, and **New text...**
writes a new one for the line.

- In the dialog, a line break is a new line, an empty line a new page (the talk box
  shows three lines a page), and `{player}` is the player's name. Other `{..}` are codes
  from the game's texts, kept as they are.
- It warns when a line is wider than the game's texts get, or a page longer than three
  lines. Accented letters (é, ñ, ü, ß...) work.
- Texts are edited in the language View > Text language shows. A new text goes to every
  language as written; switch the language to translate it.
- Fields share their text files: a change shows wherever that text is said, and the
  dialog tells how many lines say it.

### Events

An event is a script the game runs: characters walk, pose and talk, the camera moves,
and it can end by going to another field.

- **Events** in Contents lists them. **Properties** edits the script one command at a
  time: change one, move it up or down, remove it, or add one at the end.
- Places, walks and camera moves are marked on the map, numbered: drag a mark to move it.
- A **Say** command shows its text, which you can edit or write anew.
- Some events are code inside the game: those show what starts them, without a script.

**Right click > Add an event** makes a new one (numbered from 9000), starting with a walk
to where you clicked. Something has to start it: a slot of type **Event** (pick the event
in Properties and paint its trigger cells), or a character's line with the action
**Start an event**.

### Objects of your own

**Right click > Add an object from a picture...** makes an object from a picture: one
still frame, or the frames of an animation side by side, played in a loop. It shows how
the frames will be cut first. The picture gets 15 colors of its own and is anchored at
its bottom middle, where you clicked. A field has room for three pages of object
pictures, so the fields that use all three can't take more.

**Right click > Add an object from another field...** lists every object of the game's
fields (each look once, searchable by field) and copies the one you pick into this
field's own pictures, animation included, standing where you clicked.

### Characters of your own

A new character is drawn to the game's own measure: a sprite of 15 colors plus
transparency, 4 frames standing and 6 walking, seen from 5 directions (south,
south-west, west, north-west, north; the game mirrors the others).

Characters are picked by their look, from a grid of pictures named after who speaks
their lines in the game (or the name you gave them), searchable by name, field or number.

1. **Characters > Export a template...** saves a character as a sheet to draw over: a
   `.png` with a row per animation and direction and a cell per drawing. **Blank
   template...** saves an empty one with faint guides.
2. Draw over the sheet, keeping its cells.
3. **Characters > Import a character...** reads it back. It tells you what doesn't fit
   the template (a cell's size, an empty cell, more than 15 colors) instead of guessing,
   asks for a name, and offers to place it on the open field.

- **Characters > Your characters...** lists them: place one, import new drawings for it,
  export its template, rename or delete it.
- A character added to a field asks for its first line.
- Every look on a field needs an **image slot** in the sprite memory. A character added
  or changed takes a slot of its look, or a new one if there's room. A slot shared by two
  looks shows in red, with a button to give it its own.

### New fields

**File > New field** makes a field of your own from the one open: a copy of it, or an
empty map of the size you choose, walkable all over.

- It gets a mode no field has (0x2F0 on), and the banner's area and place names: the
  game's, or new ones you type.
- It runs the code of the field it came from: its music, texts and events.
- To reach it in the game, **Reach it from** leads one of the open field's exits to it,
  or you can lead an exit of any field to it later (it is in the exits' list).
- A new field has no "visited" flag (the game has those for modes up to 0x2EF only).
- **File > Delete this new field** removes it and its files.

A new field's map can also come from a 3D world built in Toyagumon3D ("Export for game",
unzipped), from the command line:

    dw3fe --new-field wstag330 --toyagumon path/to/export [--cell 64]

The picture becomes the background, scaled so a block's top is 64 pixels wide (`--cell`);
the floor comes from the world's blocks (or, for sculpted ground, from its depth), with
the edges a step too high blocked; what stands in front of a character goes to the front
layer; and the player starts on the floor nearest the middle. Paint the floor after to
fix what it got wrong.

### Sharing fields

**File > Export fields...** writes the fields you pick, as saved, to one `.dw3field`
file to share. The open field is ticked, with the new fields its exits lead to (or, for a
new field, the fields whose exits lead to it): send them together, or the exits lead
nowhere. **File > Import fields...** adds someone's file to your work folder.

- **Nothing of the game's goes in it.** A file of the field's is kept as what differs
  from the game's own, and the rest is read from the importer's disc; a text goes only
  when it is new or changed. Both of you need the same disc (Digimon World 2003, Europe):
  an import from another refuses.
- **What clashes with yours gets other numbers:** a new field whose mode you have
  already (and its files), a character of yours with the same number (one with the same
  drawings is used as it is), the texts and banner names added (an identical one already
  there is reused), and a flag the editor gave out that your fields use too. Its exits,
  lines and event scripts follow.
- **A game field you changed too is replaced** by the file's, after asking.
- Flag names come along when you haven't named that flag.

## The native port

The native port is a PC version of the game built from
[dw3_decomp](https://github.com/ReGame-Labs/dw3_decomp), a reconstruction of the game's
code. It isn't released yet. It's what plays the changes this editor saves:

- It reads them from its data folder (`data/mods`), with the new fields and events.
- **Test in game** (F5) saves and starts the port at the open field, at the story point
  above the map. The game starts fresh, without your save, so the editor also gives it
  the flags and items the characters shown at that point need (as the map shows them). With an event selected, the game plays it once the field is up; with a
  character, an object or a slot selected, the player starts beside it.

To use the editor with the port, choose **The port's data folder...** on the first run
(or start it with `dw3fe --data <the port's data folder>`) instead of your disc image.
The port's data folder needs the fields' tables, which the port writes once:

    dw2003.exe --dump-stages data/stages

## Command line

    dw3fe [wstag330]                   opens the editor (and a field)
    dw3fe --data DIR                   the folder to work in: the port's data folder,
                                       or one the editor made from the disc
    dw3fe --disc game.cue              your disc image, when the folder has no files
                                       of the game
    dw3fe --dump-disc game.cue DIR     writes the fields' tables from the disc to DIR,
                                       as the port's --dump-stages does
    dw3fe --shot out.png wstag330      draws a field's map to a picture and exits
    dw3fe --check                      reads every field and prints what it found,
                                       and the conditions and actions left as numbers
    dw3fe --selftest                   checks what saving rests on, over every field:
                                       each logic map, background tile, text and event
                                       script written and read back, the maps resized
                                       back and forth, a sprite imported, a shared
                                       file's delta
    dw3fe --encounters                 prints the game's encounters: their enemies (name,
                                       level, HP, MP) and the fields' areas with them
    dw3fe wstag245 --select actor:19   opens a field with something selected
                                       (actor:N, slot:N, object:N, event:N, start)
         [--window-shot out.png]       and saves the window and its properties
                                       (out-props.png) as drawn, then exits
    dw3fe --new-field wstag330 [--size 8x6] [--background picture.png]
                                       makes a new field from one: a copy, or an
                                       empty map of 8 x 6 tiles, its background a
                                       picture of the map's size
         [--toyagumon DIR [--cell N]]  or a Toyagumon3D game export (unzipped), a
                                       block's top N pixels wide (64)
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
    dw3fe --export-fields out.dw3field wstag330,new2f0
                                       saved fields as a file to share
    dw3fe --import-fields in.dw3field  someone's shared fields into yours

## Building from source

The editor is C++17 with Qt 6, built on Windows with MSYS2's UCRT64 toolchain.

1. **MSYS2.** Install it from [msys2.org](https://www.msys2.org), or with
   `winget install MSYS2.MSYS2`. Its installer puts it in `C:\msys64` unless you choose
   another folder.
2. **The compiler, Qt 6, CMake and Ninja.** Open the **MSYS2 UCRT64** terminal (from the
   Start menu), update MSYS2 and install them:

       pacman -Syu
       pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-qt6-base \
                 mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja

   (`pacman -Syu` may close the terminal to finish; open it again and run it once more.)
3. **Python 3.8 or newer**, from [python.org](https://www.python.org) (the Windows one,
   not MSYS2's). Optionally `pip install pillow`, which makes the exe's icon in every size
   Windows uses.
4. **Build**, from a normal Windows terminal (cmd or PowerShell) in this folder:

       python build.py

   (`py build.py` if `python` isn't on your PATH.) This writes `build\dw3fe.exe` with
   every DLL it needs next to it, so the folder runs on its own. `python build.py --clean`
   builds from scratch.

`build.py` finds MSYS2 at the folder `MSYS2_DIR` names, then where the UCRT64 `g++` on
your PATH is, then at `C:\msys64`. If you installed MSYS2 somewhere else, set it:
`set MSYS2_DIR=D:\msys64` (cmd) or `$env:MSYS2_DIR="D:\msys64"` (PowerShell).

5. **Run** `build\dw3fe.exe`, and continue as in [Getting started](#getting-started).

### The game's codes

Two files are generated from [dw3_decomp](https://github.com/ReGame-Labs/dw3_decomp)
and committed, so building doesn't need it:

- `src/core/game_codes.inc`: the story's steps (the event that moves the story on to each
  point) and the flags the game's code sets and checks, from its C;
- `src/core/stage_setups.inc`: where each field's setup code starts, from its symbols.

To write them again, clone dw3_decomp and run:

    python tools/game_codes.py path/to/dw3_decomp

(Without the path, it looks for `dw3_decomp` next to this folder.)

### Making a release

    python tools/package.py v0.3.0 [--data DIR]

builds, then packs `build/dw3-field-editor-v0.3.0-win64.zip`: the exe, its DLLs, this
README, the changelog, the license and credits, and `licenses/` with the license of every DLL, found
through the MSYS2 package it comes from. It then unpacks the zip to a temporary folder
and checks that it runs on its own and reads every field, from the data folder the
editor used last or `--data DIR`.

### Troubleshooting

- **"no g++.exe, ... in ...\ucrt64\bin"**: the packages of step 2 are missing, or MSYS2
  is somewhere else: set `MSYS2_DIR`.
- **"build/dw3fe.exe is open"**: close the editor and build again.
- **"No fields there"**: the folder has no fields. Start the editor again and pick your
  disc image, or, with the port, run `dw2003.exe --dump-stages data/stages`.
- **"isn't Digimon World 2003 (Europe, SLES-03936)'s"**: the disc image is of another
  version of the game, or damaged.

## Under the hood

### What saving writes

Only what differs from the game, in the work folder (the editor's own, or the port's
data folder):

- `mods/stages/<field>.json`: the field's tables that changed (characters, objects,
  slots, start, battles), in the form `--dump-stages` writes. The port applies it after
  the field's own setup.
- `mods/<file>.bin`: the field's file with the painted logic maps in it. The port reads
  it instead of the game's.
- `mods/<3000 + the field's number>.bin`: its background, once changed.
- `mods/<5000 + the field's number>.bin`: its objects' pictures, once a picture is added
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

### How it reads a field

- **The field's tables** (its objects, triggers, characters, battles and files) are set
  by each field's setup code, in its overlay (`AAA/PRO/WSTAGnnn.PRO` on the disc). The
  editor loads the game's executable, FIELDSTG (the field program) and the overlay where
  the game does, runs the setup on a small MIPS interpreter, and reads the tables it set
  from that memory. The port does the same natively (`--dump-stages`), and both write the
  same.
- **The background** is a file of 128-pixel tiles, each RLEN-packed: up to five sprites
  for each of three depths, then an 8-bit image of at most 256 x 128 pixels with its 256
  colors. Writing one, the editor drops what a nearer depth hides, cuts each depth in
  strips and packs them in that image.
- **The logic maps** are a tree of cells, 128 down to 8 pixels, ending in a byte per
  pixel: the floor's slope, a trigger's slot and facing, or a battle area.
- **The objects** are frames of the field's sprite sheet, drawn from its pictures in an
  emulated video memory, as the game's sprite drawer does. Their pictures are 4-bit pages
  of 256 x 256 pixels, with the palettes inside the page.
- **The characters** are the first standing frame of their animation file.
- **The text** is the game's text files, a copy for each language, through the font's
  glyphs and icons (the European letters are icons 59 on).
- **The events** are the scripts FIELDSTG's event task reads, words of commands, read
  with the tables.

## Not there yet

- **Playing your changes** needs the native port, not released yet. The editor doesn't
  write them back to a disc image for the original game or an emulator.
- **The story's events**: the game starts some events at a point of the story from a
  list of its own; a field's new events start from a slot or a line.

## License

MIT, see [LICENSE](LICENSE). Credits for the work it builds on: [CREDITS.md](CREDITS.md).
