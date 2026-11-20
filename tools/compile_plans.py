"""Generate the renderer's UTF-16 alignment table from editorial source data."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def pairs(values):
    return '{' + ', '.join('{%d,%d}' % (a,b) for a,b in values) + '}'


def generate(released_data=False):
    doc=json.loads((ROOT/'layout-audit/manual-plans.json').read_text('utf8'))
    output=[]
    seen=set()
    for plan in doc['plans']:
        jp,cn=plan['ja_length'],plan['zh_length']
        anchors=plan['anchors']
        if anchors[0]!=[0,0] or anchors[-1]!=[jp,cn] or any(
            a>=b or x>=y for (a,x),(b,y) in zip(anchors,anchors[1:])):
            raise ValueError(f'Invalid anchors at {plan["index"]}')
        hashes=plan['ja_fnv64'],plan['zh_fnv64']
        if hashes in seen:
            raise ValueError('Duplicate reviewed text')
        seen.add(hashes)
        whole=str(plan.get('mode','').startswith('whole')).lower()
        snaps=plan.get('source_boundary_snaps',[])
        merge=str(plan['merge_tail_from']) if 'merge_tail_from' in plan else 'SIZE_MAX'
        output.append(f'    {{{jp}, {cn}, 0x{int(hashes[0],16):016x}ull, '
                      f'0x{int(hashes[1],16):016x}ull, {pairs(anchors)}, '
                      f'{pairs(snaps)}, {whole}, {merge}, {str(plan.get("mode")=="highlight-only").lower()}}},')
    dest=ROOT/'build/reviewed_plans.inc'
    dest.parent.mkdir(exist_ok=True)
    dest.write_text('\n'.join(output)+'\n','utf8')

    glosses=json.loads((ROOT/'data-prep/sentence-glosses.json').read_text('utf8'))
    if released_data:
        from full_game import chinese_caption
        from validate_full_game import read_story
        keys={key for gloss in glosses for key in gloss['keys']}
        data=(ROOT/'package/mahoyo-learning/local/story.dat').read_bytes()
        translations={i:cn for i,(_,cn,_) in enumerate(read_story(data)) if i in keys}
    else:
        translations=(ROOT/'corpus/sumisora-steam-zh.txt').read_text('utf-8-sig').splitlines()
    used=set()
    lines=[]
    for gloss in glosses:
        literal=json.dumps(gloss['gloss'],ensure_ascii=False)
        expected=chinese_caption(gloss['expected']) if released_data else gloss['expected']
        for key in gloss['keys']:
            if key in used or translations[key].replace('@o','').strip()!=expected:
                raise ValueError(f'Stale or repeated sentence gloss at {key}')
            used.add(key)
            lines.append(f'    {{{key}, {{L{literal}, {str(gloss.get("with_caption",False)).lower()}}}}},')
    (dest.parent/'sentence_glosses.inc').write_text('\n'.join(lines)+'\n','utf8')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--released-data',action='store_true')
    generate(parser.parse_args().released_data)
