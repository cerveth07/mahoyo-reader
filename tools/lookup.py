"""Look up a native text key or a literal word in the private project corpus."""
from pathlib import Path
import argparse, json, re, sys
from text_pairs import apply_reviewed_pairs
p=argparse.ArgumentParser(description=__doc__)
q=p.add_mutually_exclusive_group(required=True);q.add_argument('--index',type=int);q.add_argument('--word');p.add_argument('--limit',type=int,default=10);a=p.parse_args()
if a.limit<1:p.error('--limit must be positive')
if a.word is not None and not a.word.strip():p.error('--word must not be empty')
if hasattr(sys.stdout,'reconfigure'):sys.stdout.reconfigure(encoding='utf-8')
r=Path(__file__).resolve().parents[1]/'corpus';ja=(r/'steam-ja.ctd').read_bytes().decode('utf-8-sig').split('\r\n');zh=(r/'sumisora-steam-zh.txt').read_bytes().decode('utf-8-sig').splitlines()
if ja[-1]=='':ja.pop()
if len(ja)!=len(zh):raise SystemExit('Different JA/ZH key counts')
commit=json.loads((r/'manifest.json').read_text('utf8'))['translation_commit']
apply_reviewed_pairs(ja, zh, r.parent/'data-prep/reviewed-text-pairs.json', commit)
if a.index is not None:
    if not 0<=a.index<len(ja):p.error(f'index must be within 0..{len(ja)-1}')
    hits=[a.index]
else:
    for n in json.loads((r/'anki-jlpt-n1-n2.json').read_text(encoding='utf-8')):
        if n['term']==a.word:print(' / '.join(n['levels']), n['term'], n['reading'], n['definition_zh'])
    hits=[i for i,t in enumerate(ja) if a.word in re.sub(r'<([^<>|]+)\|[^<>]+>',r'\1',t)]
    print(f'{len(hits)} matching text keys')
for i in hits[:a.limit]:print(f'[{i:06d}] {ja[i]}\n         {zh[i]}')
