"""Build the separately downloadable short-gloss pack from Tomoshi's open SQLite release."""
import argparse
import hashlib
import json
import sqlite3
import struct
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED, ZipInfo

from validate_full_game import read_story

ROOT = Path(__file__).resolve().parents[1]
RELEASE = 'https://github.com/tomoshi-app/tomoshi-dict-data/releases/tag/v2026-11-16'
SOURCE_SHA = '793d26a8963bff8b4dc39e83190d6f1608449f25aa5a8b5f9163c44e8c39b684'
DATABASE_SHA = '86d95f8f0760ce6cc8505c467c6b5613010a3ba8a0ea4c9e9427eb521f0e29bc'


def wanted_forms():
    local = ROOT/'package/mahoyo-learning/local'
    forms = set()
    with (local/'word-index.dat').open('rb') as f:
        def num(): return struct.unpack('<I',f.read(4))[0]
        def string(): return f.read(num()*2).decode('utf-16le')
        assert f.read(8)==b'MHLEX001'
        string()
        for _ in range(num()):
            base, reading, _ = string(),string(),string()
            forms.add((base,reading))
    for _,_,cards in read_story((local/'story.dat').read_bytes()):
        forms.update((w[2],w[3]) for w in cards)
    return forms


def main(db, directory):
    with db.open('rb') as source:
        if hashlib.file_digest(source, 'sha256').hexdigest() != DATABASE_SHA:
            raise ValueError('SQLite database does not match the pinned open data release.')
    wanted=wanted_forms()
    terms={t for t,_ in wanted}
    rows=set()
    with sqlite3.connect(db.resolve().as_uri()+'?mode=ro',uri=True) as conn:
        for meta, translations in conn.execute("SELECT e.data, z.data FROM entries e JOIN zh_defs z ON z.entry_id=e.id AND z.locale='zh-CN'"):
            entry=json.loads(meta)
            spellings={k['text'] for k in entry['kanji']}
            readings={k['text'] for k in entry['kana']}
            if not (spellings|readings)&terms:
                continue
            senses=json.loads(translations).get('senses',{})
            for i,sense in enumerate(entry['senses']):
                zh=senses.get(str(i),{})
                gloss='、'.join(g['text'].strip() for g in zh.get('glosses',[]) if g.get('text','').strip())
                gloss=' '.join(gloss.split())
                if not gloss:continue
                for kana in entry['kana']:
                    y=kana['text']
                    if sense.get('stag_r') and y not in sense['stag_r']:continue
                    if sense.get('restricted_to_kana') and y not in sense['restricted_to_kana']:continue
                    possible=set(kana.get('restricted_to') or spellings)
                    if sense.get('restricted_to_kanji'):possible.intersection_update(sense['restricted_to_kanji'])
                    for term in possible|{y}:
                        if (term,y) in wanted:rows.add((term,y,gloss))
    text='词条\t读音\t释义\n'+''.join('\t'.join(r)+'\n' for r in sorted(rows))
    license_text=(directory/'tomoshi-LICENSE.md').read_text('utf8')
    notice=(directory/'tomoshi-NOTICE.md').read_text('utf8')
    readme=('魔夜读本可选日中词典\n\n在读本的「词典」中选择「导入词典」，选中本 ZIP。\n'
            '来源为 Tomoshi (Y1Z) 及 EDRDG 的 JMdict，使用 entries、zh_defs 两张 CC BY-SA 4.0 表。\n'
            '本次加工按游戏词形与读音筛选，简体中文义项按行整理。该词表采用 CC BY-SA 4.0。\n'
            'https://creativecommons.org/licenses/by-sa/4.0/\n'+RELEASE+'\n'
            '官方压缩数据库 SHA-256 '+SOURCE_SHA+'\n')
    out=ROOT/'dist/mahoyo-ja-zh-optional.zip'
    with ZipFile(out.with_suffix('.tmp'),'w') as archive:
        for name,value in [('dictionary.tsv',text),('README.txt',readme),('LICENSE.md',license_text),('NOTICE.md',notice)]:
            i=ZipInfo(name,(2026,11,16,0,0,0));archive.writestr(i,value.encode('utf8'),compress_type=ZIP_DEFLATED,compresslevel=9)
    out.with_suffix('.tmp').replace(out)
    print(f'{out}: {len({r[:2] for r in rows})} spelling-reading pairs, {len(rows)} gloss rows, {out.stat().st_size} bytes')


if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('database',type=Path)
    p.add_argument('notices',type=Path)
    args=p.parse_args()
    main(args.database,args.notices)
