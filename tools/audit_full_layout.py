"""Replay the production renderer at three widths for the full private text table."""
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse
import json
import struct
import subprocess
import tempfile

from full_game import ROOT, chinese_caption, decode_japanese, load_sources, write_string


PARTICLES = '的了着过地得吗呢吧啊们之'
OPENING_QUOTES = '（《【〈「『“‘(['

def break_issues(lines):
    particles = sum(bool(line) and line[0] in PARTICLES and not line.startswith('之前') for line in lines[1:])
    quotes = sum(bool(line) and line[-1] in OPENING_QUOTES for line in lines[:-1])
    return particles, quotes


def audit_batch(exe, folder, rows):
    source = folder / f'{rows[0][0]}.input'
    output = source.with_suffix('.jsonl')
    with source.open('wb') as out:
        out.write(b'MHLAY001' + struct.pack('<I',len(rows)))
        for index, ja, cn in rows:
            out.write(struct.pack('<I',index))
            write_string(out, ja)
            write_string(out, cn)
    subprocess.run([exe,source,output,'anchored'], stdout=subprocess.DEVNULL, check=True)
    counts = Counter()
    issues = []
    flagged = {}
    layouts = []
    with output.open(encoding='utf8') as handle:
        for line in handle:
            row=json.loads(line)
            layouts.append({k:row[k] for k in ('index','profile','cn_lines','pixel_height','effective_px',
                                                'compressed','overflow','hand_reviewed')})
            particles, quotes = break_issues(row['cn_lines'])
            counts['leading_particles'] += particles
            counts['trailing_open_quotes'] += quotes
            counts['overflow'] += int(row['overflow'])
            counts['layouts'] += 1
            for key in ('compressed','needs_review','punctuation_fragment'):
                counts[key] += int(row[key])
            if row['compressed']:
                flagged.setdefault(row['index'],set()).add('compressed')
            if row['needs_review']:
                flagged.setdefault(row['index'],set()).add('check_alignment')
            if not row['complete'] or row['overflow'] or not row['height_fits_without_resampling']:
                issues.append({k:row[k] for k in ('index','profile','complete','overflow','height_fits_without_resampling')})
    if counts['layouts'] != len(rows)*3:
        raise ValueError('Incomplete audit output')
    source.unlink()
    output.unlink()
    return counts,issues,flagged,layouts


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--jobs',type=int,default=6)
    p.add_argument('--rows', type=Path, help='Save layout rows as JSON for a baseline or comparison')
    p.add_argument('--baseline', type=Path, help='Compare against an earlier --rows JSON file')
    p.add_argument('--report', type=Path, default=ROOT/'verification/full-game-layout.json')
    args=p.parse_args()
    manifest,ja,cn,_=load_sources()
    rows=[(i,decode_japanese(t)[0],chinese_caption(cn[i])) for i,t in enumerate(ja) if t.strip()]
    counts=Counter()
    issues=[]
    flagged={}
    layouts = []
    with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder, ThreadPoolExecutor(max_workers=args.jobs) as pool:
        batches=[rows[start:start+96] for start in range(0,len(rows),96)]
        for num,(c,bad,review,batch_layouts) in enumerate(pool.map(lambda batch:audit_batch(ROOT/'build/layout_audit.exe',Path(folder),batch),batches),1):
            counts.update(c)
            layouts.extend(batch_layouts)
            issues.extend(bad)
            flagged.update(review)
            if num%10==0:
                print(f'{num}/{len(batches)} batches',flush=True)
    report=dict(kind='offline GDI, modelled Japanese geometry, anchored mode',
                text_records=len(rows),intentional_blank_keys=len(ja)-len(rows),counts=dict(counts),issues=issues)
    report['compressed_keys']=[k for k,v in flagged.items() if 'compressed' in v]
    report['semantic_review_keys']=[k for k,v in flagged.items() if 'check_alignment' in v]
    report['reviewed_break_violations']=[row for row in layouts if row['hand_reviewed'] and any(break_issues(row['cn_lines']))]
    for flag in ('compressed','overflow'):
        report[flag+'_records']=len({row['index'] for row in layouts if row[flag]})
    if args.rows:
        args.rows.write_text(json.dumps(layouts,ensure_ascii=False)+'\n','utf8')
    if args.baseline:
        baseline={(row['index'],row['profile']):row for row in json.loads(args.baseline.read_text('utf8'))}
        if baseline.keys()!={(row['index'],row['profile']) for row in layouts}:
            raise ValueError('Baseline uses different records or profiles')
        report['changed_layouts']=[]
        regressions={flag:set() for flag in ('smaller_font','lower_effective_px','new_compression','new_overflow')}
        for row in layouts:
            old=baseline[row['index'],row['profile']]
            if old!=row:
                report['changed_layouts'].append(dict(index=row['index'],profile=row['profile'],before=old,after=row))
            for flag,regressed in (
                ('smaller_font',row['pixel_height']<old['pixel_height']),
                ('lower_effective_px',row['effective_px']<old['effective_px']),
                ('new_compression',row['compressed'] and not old['compressed']),
                ('new_overflow',row['overflow'] and not old['overflow']),
            ):
                if regressed:
                    regressions[flag].add(row['index'])
        report['regressions']={flag:sorted(keys) for flag,keys in regressions.items()}
    target=args.report
    target.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n','utf8')
    print(target,dict(counts),'exceptions',len(issues))
    if issues:
        raise SystemExit(1)
