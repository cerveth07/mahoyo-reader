"""Pack the original Shogakukan EBXA-C volume and a portable look-up index."""
import argparse
import base64
from collections import defaultdict
import gzip
import hashlib
import html
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unicodedata
from zipfile import ZIP_DEFLATED, ZIP_STORED, ZipFile, ZipInfo
import zlib

ROOT = Path(__file__).resolve().parents[1]
VOLUME = {
    'CATALOG': '2c696795e4a767ecfc8c41e6ace687d8f3a6abe592c0d31bcd6b6469d0a20363',
    'LANGUAGE.ebz': '19b3f2458f5d9f02c776568245449454ed134998a6ce6eff6ccfbfe1a8818d3f',
    'WELCOME': '11bd14b8ae58de301e952ba0edf825ca6dd840aa26316effa9280630a850ae41',
    'ZHONG_RI/START.ebz': 'c85c15527af82b36cc493ad5a7c947d277d6bf114aff30e3ebdb62670016305f',
}
TAG = re.compile(r'<[^>]*>')
HEAD = re.compile(r'<span class="head">(.*?)</span>', re.S)
WRITTEN = re.compile(r'<img[^>]*hA663\.bmp"[^>]*>(.*?)<img[^>]*hA664\.bmp"', re.S | re.I)
NUMBER = re.compile(r'<b>\s*[０-９0-9①-⑳]+\s*</b>', re.I)
BR = re.compile(r'<br\s*/?>', re.I)
GLOSS_TOKEN = re.compile(r'&#(\d+);|([,，;；。\.])')


def clean(fragment):
    return ' '.join(html.unescape(TAG.sub('', fragment)).split())


def search_keys(volume):
    # This title is EBZip level 0, with one 2 KiB page per compressed block.
    def page(n):
        begin, end = struct.unpack_from('>II', volume, 22 + 4 * (n - 1))
        data = volume[begin:end]
        return data if len(data) == 2048 else zlib.decompress(data)

    header = page(1)
    keys = defaultdict(set)
    for i in range(header[1]):
        item = header[16+i*16:32+i*16]
        if item[0] not in (0x90, 0x91):
            continue
        first, count = struct.unpack_from('>II', item, 2)
        for n in range(first, first+count):
            raw = page(n)
            if not raw[0] & 0x80:
                continue
            assert raw[1] == 0
            pos = 4
            for _ in range(int.from_bytes(raw[2:4], 'big')):
                grouped = raw[0] & 0x10
                kind = raw[pos] if grouped else 0
                assert kind in (0, 0x80, 0xc0)
                if grouped:
                    pos += 1
                length = raw[pos]
                pos += 1
                if kind == 0x80:
                    pos += 2 + length
                    continue
                encoded = raw[pos:pos+length]
                pos += length
                address = struct.unpack_from('>IH', raw, pos)
                pos += 12
                keys[address]  # Count references even if a legacy key uses a gaiji.
                try:
                    key = bytes(x | 0x80 for x in encoded).decode('euc_jp')
                except UnicodeDecodeError:
                    continue
                keys[address].add(unicodedata.normalize('NFKC', key))
            assert pos <= len(raw)
    return keys


def senses(markup, japanese):
    body = BR.split(markup, maxsplit=1)[-1]
    if japanese:
        # Chinese translations use decimal character references. Keep separate
        # numbered senses and stop before Japanese pronunciation and examples.
        parts = NUMBER.split(body)
        found = []
        for part in parts[1:] if len(parts)>1 else parts:
            alternatives = []
            lead = TAG.sub('', part.partition('¶')[0])
            word = []
            for code, punctuation in GLOSS_TOKEN.findall(lead):
                char = chr(int(code)) if code else punctuation
                if '\u3400' <= char <= '\u9fff':
                    word.append(char)
                elif char in ',，;；。. ' and word:
                    alternatives.append(''.join(word))
                    word.clear()
            if word:
                alternatives.append(''.join(word))
            if alternatives:
                found.append('，'.join(dict.fromkeys(alternatives)))
        if found:
            return list(dict.fromkeys(found))
    # Also retain Chinese headwords with their Japanese definitions.
    text = clean(BR.sub(' ', body)) or clean(markup)
    return [text] if text else []


def pack(catalog, pages, output):
    original = {}
    for name, digest in VOLUME.items():
        raw = (catalog.parent/name).read_bytes()
        if hashlib.sha256(raw).hexdigest()!=digest:
            raise ValueError('Unexpected dictionary volume: '+name)
        original[name] = raw
    keys = search_keys(original['ZHONG_RI/START.ebz'])
    indexed = set()
    jp_count = 0
    rows = 0
    output.parent.mkdir(parents=True, exist_ok=True)
    temp = output.with_suffix('.zip.tmp')
    with ZipFile(temp, 'w') as out, gzip.open(pages, 'rt', encoding='utf-8-sig') as inp:
        for name, raw in original.items():
            out.writestr('original/'+name, raw, compress_type=ZIP_STORED)
        with tempfile.TemporaryFile(mode='w+b') as table:
            for line in inp:
                pg, off, encoded = line.split('\t', 2)
                address = (int(pg), int(off))
                if address not in keys:
                    continue
                markup = base64.b64decode(encoded).decode('utf8')
                match = HEAD.search(markup)
                title = clean(match.group(1)) if match else ''
                aliases = set(keys[address])
                if title:
                    aliases.add(title)
                japanese = address >= (19345, 2)
                for form in WRITTEN.findall(BR.split(markup, maxsplit=1)[0]):
                    aliases.update(re.split('[・･／、,]', clean(form)))
                defs = senses(markup, japanese)
                if not defs:
                    raise ValueError('No definition at '+str(address))
                valid = sorted({clean(a) for a in aliases if clean(a)})
                if not valid:
                    raise ValueError('No heading at '+str(address))
                # Each entry stores its senses once. Keys point to the same entry.
                reading = title if japanese else ''
                assert all(not any(c in field for c in '\t\n\r\x1f') for field in [*valid,reading,*defs])
                record = ['\x1f'.join(valid), reading, '\x1f'.join(defs)]
                table.write(('\t'.join(record)+'\n').encode('utf8'))
                indexed.add(address)
                jp_count += japanese
                rows += 1
            if indexed != set(keys):
                raise ValueError(f'{len(set(keys)-indexed)} indexed entries missing from text traversal')
            entry = ZipInfo('lookup.tsv')
            entry.compress_type = ZIP_DEFLATED
            table.seek(0)
            with out.open(entry,'w') as dest:
                shutil.copyfileobj(table,dest)
        meta = dict(title='小学馆《中日・日中統合辞書》', entries=rows, japanese_entries=jp_count,
                    chinese_entries=rows-jp_count, files=VOLUME, distribution='随魔夜读本公开分发')
        out.writestr('来源.json',json.dumps(meta,ensure_ascii=False,indent=2),compress_type=ZIP_DEFLATED)
    with ZipFile(temp) as check:
        assert check.testzip() is None
    temp.replace(output)
    print(json.dumps(meta,ensure_ascii=False))


if __name__ == '__main__':
    cli=argparse.ArgumentParser(description=__doc__)
    cli.add_argument('catalog',type=Path)
    source=cli.add_mutually_exclusive_group(required=True)
    source.add_argument('--ebwin',type=Path)
    source.add_argument('--pages',type=Path,help='read a previous complete export')
    cli.add_argument('--output',type=Path,default=ROOT/'assets/dictionaries/shogakukan.zip')
    args=cli.parse_args()
    if args.pages:
        pack(args.catalog,args.pages,args.output)
    else:
        with tempfile.TemporaryDirectory() as cache:
            pages=Path(cache)/'pages.gz'
            positions=Path(cache)/'positions.tsv'
            volume=(args.catalog.parent/'ZHONG_RI/START.ebz').read_bytes()
            if hashlib.sha256(volume).hexdigest()!=VOLUME['ZHONG_RI/START.ebz']:
                raise ValueError('Unexpected START volume')
            positions.write_text(''.join(f'{p}\t{o}\n' for p,o in sorted(search_keys(volume))),encoding='ascii')
            subprocess.run(['powershell.exe','-NoProfile','-File',str(ROOT/'tools/export_shogakukan.ps1'),
                            '-Catalog',str(args.catalog.resolve()),'-EBWin',str(args.ebwin.resolve()),
                            '-Output',str(pages),'-Positions',str(positions),'-Cache',cache],check=True)
            pack(args.catalog,pages,args.output)
