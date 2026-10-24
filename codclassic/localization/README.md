# Localization tools

Tools to add Cyrillic to the game's fonts and to build a replacement `pak_x360.pk3` with translated strings.
**No translation text and no game data are included in this repository**: the tools work on files from your own
copy of the game and on a translation you provide.

- `make_fonts.py <fonts dir from your pak.pk3> <out dir>` — adds Cyrillic glyphs (Windows-1251 slots) to the
  game's Quake 3 style fonts (`fonts/fontImage_<size>.dat`) and writes new texture pages
  `fontImage_ru_<size>.dds`. Glyphs are rendered with URW C059 (`fonts-urw-base35`) and DejaVu Sans Bold
  (`fonts-dejavu-core`); set `URW_DIR` / `DEJAVU_DIR` if they are elsewhere. Needs Python 3, Pillow, NumPy.
- `build_ru.py <english localizedstrings dir> <translation dir> <fonts out dir> <pak_x360.pk3> <out.pk3>` —
  writes every `.str` file in Windows-1251 with the translation applied (strings without a translation keep
  their English text) and packs it with the fonts into a copy of `pak_x360.pk3`, the pak with the highest
  priority.

The translation directory holds one `<str file name>.txt` per string file, UTF-8, one `KEY<TAB>text` line per
string. Notes on the engine (glyph 255 is never loaded, so `я` is drawn in slot `0x60`; bytes `0x80-0x9F`
are altered by the engine) are in the comments of both scripts.

The resulting `pak_x360.pk3` contains the game's data and must not be redistributed.
