#!/usr/bin/env python3
"""Adds Cyrillic (Windows-1251 slots) to Call of Duty Classic's Quake 3 style fonts.

fonts/fontImage_<size>.dat: 256 glyphs x 80 bytes + 72 byte tail, little endian:
  int height, int width2, float top, float pitch, float xSkip, int imageWidth, int imageHeight,
  float s, t, s2, t2, int glyph (runtime handle), char shaderName[32]
height == imageHeight and width2 == imageWidth in every original entry; top is how far the cell
starts above the baseline, pitch the cell's left offset from the pen, xSkip the advance. The text
renderer draws the cell at (x + pitch, y - top) and moves x by xSkip.

Cyrillic letters whose Latin twin has the same shape (А В Е К М Н О Р С Т Х а е о р с у х) reuse
the original glyph cell; the rest are rendered with URW C059 (Century Schoolbook, the typeface
the game's font is based on) into a new texture page per size, fonts/fontImage_ru_<size>, with
cell padding and metrics calibrated against the original 'H', 'n' and 'x'. The console font
(size 18, 16x16 cells, bold sans) gets DejaVu Sans Bold.

usage: make_fonts.py <fonts dir with originals> <out dir>
"""
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

# URW base35 (fonts-urw-base35) and DejaVu (fonts-dejavu-core); override with URW_DIR / DEJAVU_DIR.
URW_DIR = os.environ.get('URW_DIR', '/usr/share/fonts/opentype/urw-base35')
DEJAVU_DIR = os.environ.get('DEJAVU_DIR', '/usr/share/fonts/truetype/dejavu')
C059 = os.path.join(URW_DIR, 'C059-Roman.otf')
C059B = os.path.join(URW_DIR, 'C059-Bold.otf')
SANS_BOLD = os.path.join(DEJAVU_DIR, 'DejaVuSans-Bold.ttf')
FMT = '<2i3f2i4fi32s'
SIZES = [12, 16, 18, 24, 30, 32]
SCALE = 4

# Windows-1251 code -> unicode, for the slots we fill.
CP = {}
for i in range(0xC0, 0x100):
    CP[i] = bytes([i]).decode('cp1251')
CP[0xA8] = 'Ё'
CP[0xB8] = 'ё'
CP[0xB9] = '№'
CP[0x85] = '…'
CP[0x96] = '–'
CP[0x97] = '—'
# Glyph 255 is never registered by the engine (Quake 3 loops i < 255): 'я' (0xFF in Windows-1251)
# is drawn in slot 0x60 ('`') instead. Slot 0x9F was tried first and the game still showed another
# character there (it does not pass bytes 0x80-0x9F through unchanged); '`' is printable ASCII and is
# not used by any of the game's strings. build_ru.py writes 'я' as 0x60.
CP[0x60] = 'я'
CP[0x93] = '“'
CP[0x94] = '”'
DONOR = {'А': 'A', 'В': 'B', 'Е': 'E', 'К': 'K', 'М': 'M', 'Н': 'H', 'О': 'O', 'Р': 'P', 'С': 'C',
         'Т': 'T', 'Х': 'X', 'а': 'a', 'е': 'e', 'о': 'o', 'р': 'p', 'с': 'c', 'у': 'y', 'х': 'x'}


def read_dat(path):
    d = open(path, 'rb').read()
    glyphs = [list(struct.unpack_from(FMT, d, 80 * i)) for i in range(256)]
    return glyphs, d[20480:]


def write_dat(path, glyphs, tail):
    out = b''.join(struct.pack(FMT, *g) for g in glyphs) + tail
    assert len(out) == 20552
    open(path, 'wb').write(out)


def ink_bbox(alpha, thr=40):
    ys, xs = np.nonzero(alpha > thr)
    if len(xs) == 0:
        return None
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1


def orig_cell(pages, g):
    page = pages[g[12].rstrip(b'\0').decode()]
    W, H = page.shape[1], page.shape[0]
    x0, y0 = round(g[7] * W), round(g[8] * H)
    x1, y1 = round(g[9] * W), round(g[10] * H)
    return page[y0:y1, x0:x1]


def render(font, ch, scale=None):
    scale = scale or SCALE
    """Glyph rendered at 4x and box-filtered down: (alpha, ink box, advance), origin-relative."""
    big = ImageFont.truetype(font.path, font.size * scale)
    asc = big.getmetrics()[0]
    w = int(big.getlength(ch)) + 8 * scale
    h = asc + big.getmetrics()[1] + 4 * scale
    im = Image.new('L', (w + 4 * scale, h), 0)
    ImageDraw.Draw(im).text((2 * scale, 0), ch, font=big, fill=255)
    a = np.array(im, dtype=np.float32)
    # Downsample (box filter).
    hh, ww = a.shape[0] // scale * scale, a.shape[1] // scale * scale
    a = a[:hh, :ww].reshape(hh // scale, scale, ww // scale, scale).mean(axis=(1, 3))
    origin_x = 2.0
    baseline = asc / scale
    return a, origin_x, baseline, big.getlength(ch) / scale


def calibrate(fontpath, ref_alpha_height, ch='H'):
    """Font size (in px) whose rendered `ch` has the same ink height as the original."""
    lo, hi = 4.0, 120.0
    for _ in range(30):
        mid = (lo + hi) / 2
        f = ImageFont.truetype(fontpath, mid)
        a, _, _, _ = render(f, ch)
        bb = ink_bbox(a, 100)
        hgt = bb[3] - bb[1]
        if hgt < ref_alpha_height:
            lo = mid
        else:
            hi = mid
    return (lo + hi) / 2


class Packer:
    def __init__(self, size):
        self.W = self.H = size
        self.img = np.zeros((size, size), np.float32)
        self.x = self.y = 1
        self.row = 0

    def place(self, a):
        h, w = a.shape
        if self.x + w + 1 > self.W:
            self.x = 1
            self.y += self.row + 1
            self.row = 0
        if self.y + h + 1 > self.H:
            return None
        self.img[self.y:self.y + h, self.x:self.x + w] = a
        pos = (self.x, self.y)
        self.x += w + 1
        self.row = max(self.row, h)
        return pos


def write_dds(path, alpha, header):
    rgba = np.zeros(alpha.shape + (4,), np.uint8)
    rgba[..., :3] = 255
    rgba[..., 3] = np.clip(alpha, 0, 255).astype(np.uint8)
    tmp = path + '.tmp.dds'
    Image.fromarray(rgba, 'RGBA').save(tmp, pixel_format='DXT5')
    data = open(tmp, 'rb').read()[128:]
    os.remove(tmp)
    # Same header as the game's own font pages (256x256 DXT5, no mipmaps).
    open(path, 'wb').write(header + data)


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for size in SIZES:
        glyphs, tail = read_dat(os.path.join(src, f'fontImage_{size}.dat'))
        pages = {}
        header = open(os.path.join(src, 'fontImage_0_24.dds'), 'rb').read(128)  # DXT5 256x256
        for g in glyphs:
            name = g[12].rstrip(b'\0').decode()
            if name and name not in pages:
                p = os.path.join(src, os.path.basename(name).replace('.tga', '.dds'))
                pages[name] = np.array(Image.open(p).convert('RGBA'))[..., 3].astype(np.float32)

        console = size == 18
        fontpath = SANS_BOLD if console else (C059B if size == 30 else C059)
        sc = 1 if size <= 16 else 4  # small sizes: FreeType hinting at native size, crisp like the originals
        H = glyphs[ord('H')]
        bbH = ink_bbox(orig_cell(pages, H))
        global SCALE
        SCALE = sc
        px = calibrate(fontpath, bbH[3] - bbH[1])
        font = ImageFont.truetype(fontpath, px)
        # Calibration of padding/offsets on 'H': original cell vs its ink.
        aH, oxH, blH, advH = render(font, 'H')
        bbm = ink_bbox(aH)
        pad_l, pad_t = bbH[0], bbH[1]
        pad_r, pad_b = H[5] - bbH[2], H[6] - bbH[3]
        # top of the original cell above baseline = H[2]; ink top of H above baseline (mine):
        ink_top_H = blH - bbm[1]
        ink_left_H = bbm[0] - oxH
        # cell top above baseline for a glyph = its ink top + pad_t + (H[2] - (ink_top_H + pad_t))
        dtop = H[2] - (ink_top_H + pad_t)
        dleft = H[3] - (ink_left_H - pad_l)
        kskip = H[4] / advH
        packer = Packer(256)
        page_name = f'fonts/fontImage_ru_{size}.tga'
        new = dict()
        for code, ch in sorted(CP.items()):
            if not console and ch in DONOR:
                new[code] = list(glyphs[ord(DONOR[ch])])
                continue
            if console and ch in DONOR:
                new[code] = list(glyphs[ord(DONOR[ch])])
                continue
            a, ox, bl, adv = render(font, ch)
            bb = ink_bbox(a)
            if console:
                # 16x16 cells like the originals: same baseline row as 'H', centered horizontally.
                cellH = orig_cell(pages, H)
                cw, chh = cellH.shape[1], cellH.shape[0]
                cell = np.zeros((chh, cw), np.float32)
                base_row = bbH[3]
                ink = a[bb[1]:bb[3], bb[0]:bb[2]]
                ih, iw = ink.shape
                iw = min(iw, cw)
                ink = ink[:, :iw]
                y_top = base_row - int(round(bl - bb[1]))
                x_left = (cw - iw) // 2
                ys, ye = max(y_top, 0), min(y_top + ih, chh)
                cell[ys:ye, x_left:x_left + iw] = ink[ys - y_top:ye - y_top]
                pos = packer.place(cell)
                g = list(H)
                g[7], g[8] = pos[0] / 256, pos[1] / 256
                g[9], g[10] = (pos[0] + cw) / 256, (pos[1] + chh) / 256
                g[12] = page_name.encode().ljust(32, b'\0')
                new[code] = g
                continue
            x0, y0 = max(bb[0] - pad_l, 0), max(bb[1] - pad_t, 0)
            x1, y1 = min(bb[2] + pad_r, a.shape[1]), min(bb[3] + pad_b, a.shape[0])
            cell = a[y0:y1, x0:x1]
            pos = packer.place(cell)
            if pos is None:
                sys.exit(f'size {size}: page full')
            h, w = cell.shape
            top = (bl - y0) + dtop
            pitch = (x0 - ox) + dleft
            xskip = adv * kskip
            if console:
                # Console font: fixed advance like the originals.
                xskip = H[4]
                pitch = H[3]
            new[code] = [h, w, float(round(top * 4) / 4), float(round(pitch * 4) / 4),
                         float(round(xskip * 4) / 4), w, h, pos[0] / 256, pos[1] / 256,
                         (pos[0] + w) / 256, (pos[1] + h) / 256, H[11],
                         page_name.encode().ljust(32, b'\0')]
        for code, g in new.items():
            glyphs[code] = g
        write_dat(os.path.join(out, f'fontImage_{size}.dat'), glyphs, tail)
        write_dds(os.path.join(out, f'fontImage_ru_{size}.dds'), packer.img, header)
        print(f'size {size}: render {px:.2f} px, page rows used {packer.y + packer.row}/256')


if __name__ == '__main__':
    main()
