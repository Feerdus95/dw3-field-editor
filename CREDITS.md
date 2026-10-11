# Credits

## dw3_decomp

`src/core/field.cpp` and `src/core/text.cpp` port what the field does with its maps,
objects, characters and text from dw3_decomp's C decompilation of the game, under its
license: the map's tiles and logic maps (`src/fieldstg/map_streamer.c`, `stream.c`,
`map.c`), the objects and characters' sprites (`map_objects.c`, `actor_control.c`,
`src/main/gfx/sprite_drawer.c`, `tim_loader.c`, `src/main/file/decompressor.c`), the
text (`src/main/text/font.c`, `text_tools.c`, `text_window.c`) and the tables' layouts
(`include/field_map.h`, `include/dw3/game_state.h`). `src/core/codes.cpp` ports the
conditions and actions (`src/main/game/events.c`, its special conditions and their
tables, `src/main/game/party.c`), and `src/core/game_codes.inc` is written from the
stages' code (`src/stages/`) by `tools/game_codes.py`.

```
MIT License

Copyright (c) 2026 juandav

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```


## Qt

Qt 6 (Widgets), linked dynamically as its DLLs: LGPL v3, see https://www.qt.io/licensing.
The release ships Qt's DLLs unmodified, with the DLLs they need as MSYS2 builds them,
each under its own license: the GCC runtime (GPL with the runtime library exception),
GLib, libintl and libiconv (LGPL), and ICU, PCRE2, HarfBuzz, FreeType, Graphite,
libpng, libjpeg, zlib, zstd, Brotli, bzip2, libb2, libffi, double-conversion and MD4C
(permissive licenses). Each one's license text is in the release's `licenses/`
folder, by the MSYS2 package it comes from.
