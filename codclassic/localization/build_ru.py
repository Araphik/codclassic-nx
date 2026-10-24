#!/usr/bin/env python3
"""Builds the Russian pak_x360.pk3 for Call of Duty Classic (Xbox 360 data, Switch port).

The game's search order is pak_x360.pk3 > pak.pk3 > main/ > localized_english_pak.pk3, and the
fonts live in pak.pk3, so the Russian files go into a copy of pak_x360.pk3 (the highest priority
pak): the original entries plus
  fonts/fontImage_<size>.dat       Cyrillic glyphs in the Windows-1251 slots (make_fonts.py)
  fonts/fontImage_ru_<size>.dds    their texture pages
  localizedstrings/english/*.str   every string file, translated (ru/<file>.txt) and in Windows-1251
The game keeps running in "english", so the files keep LANG_ENGLISH.

Strings with no translation keep their English text; characters whose byte now draws a Cyrillic
letter (accented Latin) are reduced to plain ASCII.

usage: build_ru.py <english localizedstrings dir> <ru dir> <fonts out dir> <pak_x360.pk3> <out.pk3>
"""
import os
import re
import sys
import unicodedata
import zipfile

ENTRY = re.compile(r'^(REFERENCE\s+(\S+)\s*\n(?:(?!REFERENCE).*\n)*?\s*LANG_ENGLISH\s+")(.*)("\s*)$', re.M)
# Byte values that now hold Cyrillic (or new punctuation) glyphs.
REMAPPED = set(range(0xC0, 0x100)) | {0xA8, 0xB8, 0xB9, 0x85, 0x93, 0x94, 0x96, 0x97}


def decode_original(raw):
    try:
        return raw.decode('utf-8')
    except UnicodeDecodeError:
        return raw.decode('latin-1')


def to_cp1251(text, keep_cyrillic):
    out = bytearray()
    for ch in text:
        if ch == 'я':
            out += b'`'  # glyph 255 is never loaded by the engine; see make_fonts.py
            continue
        try:
            b = ch.encode('cp1251')
        except UnicodeEncodeError:
            b = None
        if b is not None and (keep_cyrillic or b[0] not in REMAPPED or b[0] < 0x80):
            out += b
            continue
        # Accented Latin and other leftovers: strip to ASCII.
        base = unicodedata.normalize('NFKD', ch).encode('ascii', 'ignore')
        out += base if base else b'?'
    return bytes(out)


def load_ru(path):
    tr = {}
    for line in open(path, encoding='utf-8'):
        line = line.rstrip('\n')
        if not line:
            continue
        k, v = line.split('\t', 1)
        tr[k] = v
    return tr


def build_str(src_path, ru_path):
    text = decode_original(open(src_path, 'rb').read())
    tr = load_ru(ru_path) if os.path.exists(ru_path) else {}
    used = set()

    def repl(m):
        key, en = m.group(2), m.group(3)
        if key in tr:
            ru = tr[key]
            # Keep the English trailing spaces (menu labels are padded with them).
            pad = en[len(en.rstrip(' ')):]
            ru = ru.rstrip(' ') + pad
            used.add(key)
            return m.group(1) + ru + m.group(4)
        return m.group(0)

    out = ENTRY.sub(repl, text)
    missing = set(tr) - used
    if missing:
        print(f'  {os.path.basename(src_path)}: {len(missing)} keys not found: {sorted(missing)[:5]}')
    # Translated text is Cyrillic; everything else (English leftovers) must not hit remapped bytes.
    data = bytearray()
    for line in out.splitlines(keepends=True):
        cyr = any('Ѐ' <= c <= 'ӿ' for c in line)
        data += to_cp1251(line, keep_cyrillic=cyr)
    return bytes(data), len(used)


def main():
    en_dir, ru_dir, fonts_dir, pak_in, pak_out = sys.argv[1:6]
    files = {}
    total = 0
    for name in sorted(os.listdir(en_dir)):
        if not name.endswith('.str'):
            continue
        data, n = build_str(os.path.join(en_dir, name), os.path.join(ru_dir, name[:-4] + '.txt'))
        files[f'localizedstrings/english/{name}'] = data
        total += n
    for name in sorted(os.listdir(fonts_dir)):
        if name.endswith(('.dat', '.dds')):
            files[f'fonts/{name}'] = open(os.path.join(fonts_dir, name), 'rb').read()
    src = zipfile.ZipFile(pak_in)
    with zipfile.ZipFile(pak_out, 'w', zipfile.ZIP_DEFLATED) as z:
        for info in src.infolist():
            if info.filename in files:
                continue
            z.writestr(info, src.read(info.filename), compress_type=info.compress_type)
        for name, data in files.items():
            z.writestr(name, data)
    print(f'{total} strings translated, {len(files)} files added to {pak_out}')


if __name__ == '__main__':
    main()
