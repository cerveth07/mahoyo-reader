"""Independently decode the shipping file and compare every source key and span."""
import argparse
import hashlib
import json
import re
import struct
from collections import Counter, defaultdict
from io import BytesIO
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOCAL = ROOT / 'package/mahoyo-learning/local'
CORPUS = ROOT / 'corpus'


# Review cases use exact UTF-16 spans to distinguish repeated surfaces within a key.
READING_CASES = {
    3: [(5,6,'何','なに')],
    54: [(13,15,'言う','いう')],
    67: [(1,2,'何','なに')],
    68: [(3,6,'十分間','じゅっぷんかん'),(10,11,'何','なに')],
    74: [(1,2,'私','わたし')],
    168: [(4,7,'一時間','いちじかん'),(16,17,'何','なん')],
    247: [(14,16,'言う','いう')],
    326: [(21,23,'一割','いちわり')],
    377: [(9,10,'何','なに')],
    449: [(11,13,'言う','いう')],
    554: [(1,5,'何げない','なにげない')],
    708: [(73,74,'何','なん')],
    1023: [(13,15,'一件','いっけん')],
    1059: [(14,16,'私達','わたしたち')],
    1067: [(7,10,'一週間','いっしゅうかん'),(38,40,'一番','いちばん')],
    1299: [(15,16,'他','ほか')],
    1514: [(15,16,'他','た')],
    1652: [(1,2,'他','ほか')],
    2164: [(8,10,'一分','いっぷん'),(10,12,'一秒','いちびょう')],
    2410: [(43,46,'一ヶ月','いっかげつ')],
    2492: [(3,5,'何故','なぜ')],
    3602: [(21,25,'何気なく','なにげなく')],
    3995: [(15,17,'一点','いってん')],
    6894: [(17,19,'一周','いっしゅう')],
    9282: [(1,3,'一晩','ひとばん')],
    9553: [(8,9,'他','た')],
    16966: [(14,17,'一本道','いっぽんみち')],
    21499: [(8,10,'一組','ひとくみ')],
}
LEMMA_CASES = {
    48: (20,23,'分から','辞書形：分かる（わかる）'),
    51: (30,33,'分かっ','辞書形：分かる（わかる）'),
    53: (16,18,'思っ','辞書形：思う（おもう）'),
    55: (19,21,'言っ','辞書形：言う（いう）'),
    58: (15,17,'言わ','辞書形：言う（いう）'),
    3602: (21,25,'何気なく','辞書形：何気ない（なにげない）'),
}


def read_story(raw):
    stream = BytesIO(raw)

    def u32():
        return struct.unpack('<I', stream.read(4))[0]

    def string():
        size = u32()
        assert size <= 100000
        data = stream.read(size * 2)
        assert len(data) == size * 2
        value = data.decode('utf-16le')
        assert '\x00' not in value
        return value

    assert stream.read(8) == b'MHJL0002'
    count = u32()
    for index in range(count):
        assert u32() == index
        source, caption = string(), string()
        words = [(u32(),u32(),*(string() for _ in range(6))) for _ in range(u32())]
        yield source, caption, words
    assert not stream.read(1)


def validate(baseline=None):
    sources = json.loads((CORPUS/'manifest.json').read_text('utf8'))
    prov = json.loads((LOCAL/'provenance.json').read_text('utf8'))
    for name, digest in sources['files'].items():
        assert hashlib.sha256((CORPUS/name).read_bytes()).hexdigest() == digest
    original = (CORPUS/'steam-ja.ctd').read_text('utf-8-sig').splitlines()
    translated = (CORPUS/'sumisora-steam-zh.txt').read_text('utf-8-sig').splitlines()
    pairs_path = ROOT/'data-prep/reviewed-text-pairs.json'
    assert hashlib.sha256(pairs_path.read_bytes()).hexdigest() == prov['pairing_sha256']
    overrides_path = ROOT/'data-prep/reading-overrides.json'
    assert hashlib.sha256(overrides_path.read_bytes()).hexdigest() == prov['reading_overrides_sha256']
    contextual_overrides = json.loads(overrides_path.read_text('utf8'))['by_key']
    word_edits_path = ROOT/'data-prep/reviewed-word-cards.jsonl'
    assert hashlib.sha256(word_edits_path.read_bytes()).hexdigest() == prov['reviewed_word_cards_sha256']
    word_edits = defaultdict(list)
    seen_edits = set()
    for line in word_edits_path.read_text('utf8').splitlines():
        edit = json.loads(line)
        key, span = edit['key'], tuple(edit['span'])
        assert 0 <= key < len(original) and (key, span) not in seen_edits
        seen_edits.add((key, span))
        word_edits[key].append(edit)
    assert len(seen_edits) == prov['reviewed_word_cards']
    notes = {n['note_id']: n for n in json.loads((CORPUS/'anki-jlpt-n1-n2.json').read_text('utf-8-sig'))}
    indices = list(range(len(translated)))
    reviewed = json.loads(pairs_path.read_text('utf8'))
    for entry in reviewed['swaps']:
        a,b = entry['keys']
        indices[a],indices[b] = indices[b],indices[a]
    translated = [translated[i] for i in indices]
    assert translated[21091].strip() == '然后。'
    assert '迷途的羔羊' in translated[21092]
    edits = reviewed['caption_edits']
    assert len({entry['key'] for entry in edits}) == len(edits)
    for entry in edits:
        key = entry['key']
        assert 0 <= key < len(original) and original[key] == entry['expected_japanese']
        assert translated[key] == entry['expected_chinese']
        translated[key] = entry['replacement']
    raw = (LOCAL/'story.dat').read_bytes()
    count = len(original)
    assert count == len(translated) == prov['records'] == sources['native_text_keys']
    story = read_story(raw)
    previous = read_story(baseline.read_bytes()) if baseline else None
    assert hashlib.sha256(raw).hexdigest() == prov['sha256']
    assert len(raw) == prov['bytes'] and prov['source_hashes'] == sources['files']
    levels = Counter()
    checks = Counter()
    for index, (jp, cn) in enumerate(zip(original, translated)):
        source, caption, words = next(story)
        assert source == jp
        if previous is not None:
            old_source, old_caption, old_words = next(previous)
            assert (source,caption) == (old_source,old_caption), index
            assert [w for w in words if w[6] != '补注'] == [w for w in old_words if w[6] != '补注'], index
            checks['baseline_keys_unchanged'] += 1
        body = re.sub(r'<([^<>|]*)\|[^<>]*>', r'\1', jp).replace('@o','').replace('^','')
        cn = cn.replace('@o','').replace('^','')
        fragments = re.split(r'(<[^<>]+>)', cn)
        for k, fragment in enumerate(fragments):
            if fragment.startswith('<'):
                base, gloss = fragment[1:-1].split('|')
                latin = re.search('[a-zA-Z]',base) and not re.search('[㐀-鿿]',base)
                if latin and re.search('[㐀-鿿]',gloss):
                    fragments[k] = gloss
                    fragments[k+1] = re.sub('^[a-zA-Z]+','',fragments[k+1])
                else:
                    fragments[k] = base
        assert caption == ''.join(fragments), index
        assert not re.search(r'[@<>|^]',caption), index
        if body.strip():
            checks['visible_source_keys'] += 1
        else:
            assert not caption.strip()
            checks['intentional_blank_keys'] += 1
        found = []
        reading_units = set()
        supplemental = {}
        reviewed_hits = {}
        units = body.encode('utf-16le')
        for start,end,word,reading,pos,meaning,level,pitch in words:
            assert 0 <= start < end <= len(units)//2
            assert units[2*start:2*end].decode('utf-16le') == word, (index,word)
            assert '@p' not in reading and '@_' not in reading
            if '草十郎' in body and word == '草' and level != '原作':
                assert not units[2*start:2*(start+3)].decode('utf-16le').startswith('草十郎')
            if level == '补注':
                assert re.fullmatch(r'[ぁ-ゖァ-ヺーゝゞヽヾ]+', reading), (index, word, reading)
                assert not pitch and (start,end) not in supplemental
                supplemental[start,end] = (word,reading,meaning)
                lines = meaning.splitlines()
                if any(line and not line.startswith(('辞書形：','原作注音：')) for line in lines):
                    checks['supplemental_definitions'] += 1
                for line in lines:
                    if line.startswith('辞書形：'):
                        assert re.fullmatch(r'辞書形：.+（[ぁ-ゖァ-ヺーゝゞヽヾ]+）',line), (index,word,line)
                        checks['supplemental_lemmas'] += 1
                explicit = contextual_overrides.get(str(index), {}).get(word)
                if not explicit or explicit[0] != reading:
                    assert (word,reading) not in {('私','わたくし'),('言う','ゆう')}, index
                    if word == '何':
                        following = units[2*end:].decode('utf-16le')
                        if following.startswith(('か','も','を','が','に','へ','より')):
                            assert reading == 'なに', (index,following)

            elif level != '原作':
                assert level in ('N1','N2') and meaning
            plain_reading = re.sub(r'[\s・、。]', '', reading)
            if re.fullmatch(r'[ぁ-ゖァ-ヺーゝゞヽヾ]+', plain_reading) or (
                level in ('N1','N2') and re.search(r'[ぁ-ゖァ-ヺ]', reading)
            ):
                reading_units.update(range(start, end))
            if level in ('N1','N2'):
                reviewed_hits.setdefault((start,end), []).append((word,reading,pos,meaning,level,pitch))
                if pitch and '（原作） / ' in reading and reading.endswith('（辞書）'):
                    assert pitch.startswith('辞书重音 '), (index, word, pitch)
            levels[level] += 1
            found.append((word,reading,meaning,level))
        for start,end,word,reading in READING_CASES.get(index, ()):
            assert supplemental.get((start,end), ())[:2] == (word,reading), (index,word,reading)
            assert not any((left,right)!=(start,end) and start<right and left<end
                           for left,right in supplemental), (index,word,'split reading cards')
        if index in LEMMA_CASES:
            start,end,word,heading = LEMMA_CASES[index]
            assert supplemental[start,end][0] == word and heading in supplemental[start,end][2].splitlines(), index
        for edit in word_edits.get(index, ()):
            span = tuple(edit['span'])
            note = notes[edit['note_id']]
            assert len(reviewed_hits.get(span, ())) == 1, (index, edit)
            word, reading, pos, meaning, level, _ = reviewed_hits[span][0]
            assert word == edit['surface'] and level == '/'.join(note['levels'])
            assert pos == edit.get('pos', note['part_of_speech'])
            assert edit.get('meaning', note['definition_zh']) in meaning
            expected_reading = edit.get('reading', note['reading'])
            assert reading == expected_reading or reading.endswith(f'{expected_reading}（辞書）')
            if 'old' in edit and tuple(edit['old'][:2]) != span:
                assert tuple(edit['old'][:2]) not in reviewed_hits, (index, edit)
            checks['reviewed_word_cards'] += 1
        offset = 0
        for char in body:
            if re.fullmatch(r'[㐀-鿿豈-﫿\U00020000-\U000323af々]', char):
                assert offset in reading_units, (index, offset, char)
                checks['han_char_occurrences'] += 1
            offset += len(char.encode('utf-16le'))//2
        if index in (27,693,964,4930,15989,21600,2065):
            expected = {27:('一匹','いっぴき'),4930:('蒼崎','あおざき'),15989:('有珠','ありす'),
                        21600:('土桔','ときつ'),2065:('人気','ひとけ'),693:('一日','いちにち'),
                        964:('今日','きょう')}[index]
            assert any(x[:2]==expected for x in found), (index, expected)
        # Real novel passages whose token boundaries/readings were wrong in v1.
        denied = {93:'びり',299:'いっそ',506:'はまる',619:'しかも'}
        if index in denied:
            assert denied[index] not in [x[0] for x in found]
        if index == 1:
            assert any(x[0]=='厳か' and 'しずか（原作）' in x[1] for x in found)
        if index == 1096:
            assert any(x[0]=='もくもく' and '默默' in x[2] for x in found)
        if index == 4165:
            assert '我活在睡梦之中' in caption and any('私は眠り' in x[1] for x in found)
        assert sum(x[-1]=='原作' for x in found) == len(re.findall(r'<[^<>|]+\|[^<>]+>',jp)), index
    assert next(story,None) is None and dict(levels) == prov['annotations']
    if previous is not None:
        assert next(previous,None) is None
    assert prov['supplemental_lemmas'] == checks['supplemental_lemmas']
    assert checks['reviewed_word_cards'] == prov['reviewed_word_cards']
    assert prov['supplemental_definitions'] == checks['supplemental_definitions']
    result = dict(records=count, sha256=prov['sha256'], annotations=dict(levels), **checks)
    print(json.dumps(result, ensure_ascii=False))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline',type=Path,help='compare story text and every existing deck/author card to this story.dat')
    validate(parser.parse_args().baseline)
