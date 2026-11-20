"""Build a private-font template and character positions, without shipping game drawings.

Only needed when changing supported game assets. Run with the installed game
directory and the same mahoyo_tools decoder used by check_script_keys.py.
Requires fontTools. The ordinary release build embeds the committed recipe.
"""
import argparse
import gzip
import hashlib
import importlib
import io
import struct
import sys
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen


def make_recipe(game, decoder, output):
    sys.path.insert(0, str(decoder.parent.resolve()))
    unpack = importlib.import_module(decoder.name + ".lenzu").lenzu_decompress
    raw = (game / "data00100.hfa").read_bytes()
    if raw[:12] != b"HUNEXGGEFA10":
        raise ValueError("Unsupported font archive")
    count, = struct.unpack_from("<I", raw, 12)
    base = 16 + count * 128
    entries = {}
    for offset in range(16, base, 128):
        name = raw[offset:offset + 96].split(b"\0")[0].decode()
        start, size = struct.unpack_from("<II", raw, offset + 96)
        entries[name] = raw[base + start:base + start + size]

    glyphs = {}
    for lang, locale in (("02", "zc"), ("00", "ja")):
        for suffix, atlas in (("0001", ""), ("0000", "H")):
            src = entries["Font" + lang + suffix + ".ccit"]
            table = unpack(io.BytesIO(src), len(src)).getvalue()
            n, stride = struct.unpack_from("<II", table)
            assert stride == 8
            pos = 8 + 8 * n
            has_rects, = struct.unpack_from("<I", table, pos)
            for char, index in struct.iter_unpack("<II", table[8:pos]):
                ch = char.to_bytes(max(1, (char.bit_length()+7)//8), "big").decode()
                if len(ch) != 1 or ord(ch) < 32 or ord(ch) > 65535 or ch in glyphs:
                    continue
                if has_rects:
                    page, x, y, w, h = struct.unpack_from("<IIIII", table, pos+8+index*20)
                    name = f"FONT_{locale}_{atlas}{page:02d}.mzp"
                else:
                    page, cell = divmod(index, 98)
                    x, y, w, h = (cell%14)*73, (cell//14)*73, 73, 73
                    name = f"FONT_{locale}_{page:03d}.mzp"
                if name not in entries or not (0<w<=100 and 0<h<=100):
                    raise ValueError((ch, name, w, h))
                glyphs[ch] = name, x, y, w, h

    # Additional UI labels and dictionary text can contain characters outside
    # the game's subset. Draw these from a local system font at installation.
    extra = set("⓪①②③④⑤⑥⑦⑧⑨⑩✓→／·…")
    for lead in range(0xb0, 0xf8):
        for trail in range(0xa1, 0xff):
            extra.update(bytes([lead, trail]).decode("gb2312", errors="ignore"))
    for ch in extra:
        glyphs.setdefault(ch, ("", 0, 0, 73, 73))
    paths = sorted({g[0] for g in glyphs.values()} - {""})
    chars = sorted(glyphs)
    names = [".notdef"] + [f"uni{ord(c):04X}" for c in chars]
    fb = FontBuilder(1024, isTTF=True)
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap({ord(ch): n for ch, n in zip(chars, names[1:])})
    fb.setupGlyf({n: TTGlyphPen(None).glyph() for n in names})
    fb.setupHorizontalMetrics({n: (1022, 0) for n in names})
    fb.setupHorizontalHeader(ascent=910, descent=-174, lineGap=18)
    fb.setupNameTable({"familyName": "Mahoyo Reader Mincho", "styleName": "Regular",
                       "uniqueFontIdentifier": "MahoyoReaderMincho-1", "fullName": "Mahoyo Reader Mincho",
                       "psName": "MahoyoReaderMincho", "version": "Version 1.0"})
    fb.setupOS2(sTypoAscender=910, sTypoDescender=-174, sTypoLineGap=18,
                usWinAscent=936, usWinDescent=200, ulCodePageRange1=(1 | 1<<17 | 1<<18))
    fb.setupPost(keepGlyphNames=False)
    fb.font.recalcTimestamp = False
    fb.font["head"].created = fb.font["head"].modified = 3850000000
    template = io.BytesIO()
    fb.save(template)
    header = template.getvalue()
    data = bytearray(b"MHFT0001" + hashlib.sha256(raw).digest() + struct.pack("<I", len(header)) + header)
    data += struct.pack("<H", len(paths))
    for name in paths:
        data += bytes([len(name)]) + name.encode("ascii")
    data += struct.pack("<H", len(chars))
    for ch in chars:
        name, x, y, w, h = glyphs[ch]
        data += struct.pack("<HBHHBB", ord(ch), paths.index(name) if name else 255, x, y, w, h)
    output.write_bytes(gzip.compress(bytes(data), mtime=0))
    print(len(chars), "characters,", sum(bool(v[0]) for v in glyphs.values()), "from game sheets")


if __name__ == "__main__":
    arg = argparse.ArgumentParser()
    for field in ("game", "decoder", "output"):
        arg.add_argument(field, type=Path)
    args = arg.parse_args()
    make_recipe(args.game, args.decoder, args.output)
