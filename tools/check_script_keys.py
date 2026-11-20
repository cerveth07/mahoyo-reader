"""Compare every compiled script's six-digit text references with the shipping table.

Uses a separately checked-out loicfrance/mahoyo_tools LenZu decoder (read only).
No uncompressed scripts or game archives are written.
"""
import argparse
import hashlib
import importlib
import io
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def entries(archive):
    with archive.open('rb') as fh:
        if fh.read(12) != b'HUNEXGGEFA10':
            raise ValueError(archive.name)
        count = struct.unpack('<I',fh.read(4))[0]
        table = fh.read(128*count)
        for offset in range(0,len(table),128):
            name = table[offset:offset+96].split(b'\0',1)[0].decode()
            start,size = struct.unpack_from('<II',table,offset+96)
            if name.endswith('.chs') or name=='script_text_ja.ctd':
                fh.seek(16+len(table)+start)
                yield name,fh.read(size)


def main(game, decoder):
    sys.path.insert(0,str(decoder.resolve().parent))
    unpack = importlib.import_module(decoder.name+'.lenzu').lenzu_decompress
    seen = set()
    scripts = {}
    data = json.loads((ROOT/'package/mahoyo-learning/local/provenance.json').read_text('utf8'))
    jp_verified = False
    for archive in sorted(game.glob('*.hfa')):
        for name,raw in entries(archive):
            if raw.startswith(b'LenZuCompressor'):
                raw = unpack(io.BytesIO(raw),len(raw)).getvalue()
            if name=='script_text_ja.ctd':
                if hashlib.sha256(raw).hexdigest() != data['source_hashes']['steam-ja.ctd']:
                    raise ValueError('Installed game text differs from pinned corpus')
                jp_verified = True
                continue
            keys = {int(key) for key in re.findall(rb'\$(\d{6})(?!\d)',raw)}
            if keys:
                scripts[archive.name+'/'+name] = [len(keys),min(keys),max(keys)]
                seen.update(keys)
    missing = sorted(set(range(data['records']))-seen)
    outside = sorted(seen-set(range(data['records'])))
    report = dict(game_text_byte_identical=jp_verified,referenced_keys=len(seen),
                  missing_from_scripts=missing,missing_from_mod=outside,script_key_count_min_max=scripts)
    (ROOT/'verification/full-game-script-keys.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print('Matching installed Japanese table:',jp_verified,'referenced:',len(seen),'scripts:',len(scripts),
          'without reference:',len(missing),'outside data:',len(outside))
    if not jp_verified or outside:
        raise SystemExit(1)


if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('game',type=Path)
    p.add_argument('decoder',type=Path)
    args=p.parse_args()
    main(args.game,args.decoder)
