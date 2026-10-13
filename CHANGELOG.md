# Changelog

## v0.3.0

- **Sharing fields:** File > **Export fields...** writes the fields you pick to one
  `.dw3field` file, and **Import fields...** adds someone's to yours. Nothing of the
  game's goes in it: files go as what differs from the game's, texts only when new or
  changed. New fields, characters, texts and the editor's flags that clash with yours
  get other numbers, and the exits, lines and scripts follow. Also `--export-fields` and
  `--import-fields`.

## v0.2.1

- **Fixed:** a battle area painted over with No battles could stay in a few pixels a
  stroke missed, too small to see, and still start battles in the game. Each painted
  area's card in **Battles** now says how much of the map it covers (in red when only a
  few pixels are left), and **Erase it from the map** clears all of it in one step.

## v0.2.0

- **Battles** in plain words: a card per painted battle area in its map color, how often
  battles come in steps, the battlefield and music named by the fields that have them,
  and who appears, by name and level, with their chances. Change them by picking from
  the game's encounters (searched by a Digimon's name), make them more or less common, or
  add more; each change is one undo step. The raw numbers are a click away.
- `--encounters` prints the game's encounters, their enemies and where they are met.
- An area with empty battles (nobody to meet) shows a warning: the game crashes when it
  picks one. Adding someone fills the empty ones first.
- Painting or erasing a battle area updates the Battles panel at once.
- Area 4 is named **Event battles**, in Paint areas and in Battles.
- **Add an object from another field...** (right click): any object of the game's
  fields, animation included, copied into this one.
- New fields from a Toyagumon3D game export (`--new-field ... --toyagumon DIR`): its
  picture as the background, the floor from its blocks or its depth, and the front layer.

## v0.1.2

- **Fixed:** at a story point, a character none of whose versions is there then (Tamer
  Natsumi in Central Park at 0x02) still showed its first version, as
  if the game had it. It now shows faint, with a tooltip saying it isn't there.
- **Fixed:** Test in game at a story point showed only the characters whose versions
  need nothing but the story point: the game started without the flags and items the
  others need (Tamer Natsumi in Central Park, from 0x04 on, needs an item). The editor now passes them,
  so the field looks as the map shows it. Needs a port that takes `--set`.

## v0.1.1

- **Fixed:** running the editor from the command line (`--check`, `--shot` and the
  others) with `--data` or `--disc` no longer changes the folder the editor opens next
  time. A folder left that way, without the game's files, made **Test in game** ask for
  the port's data folder.

## v0.1.0

The first release.

- Every field of the game as the game draws it, with its logic layers (floor, triggers,
  battle areas), exits, characters, objects and events.
- The fields read from your own disc image of the European version (SLES-03936), or from
  the native port's data folder.
- Editing: paint the logic layers; move and add characters, objects and the start; exits,
  dialogue, battles and event scripts; conditions and actions picked from lists in the
  game's names; story points and flags named, new flags.
- Things of your own: characters from a drawn template, objects from pictures, new
  backgrounds, bigger maps, new fields.
- Texts in English, French, Italian, German and Spanish, edited and written anew.
