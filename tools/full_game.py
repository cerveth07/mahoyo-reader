"""Build the study edition from the three pinned full-game sources."""
import hashlib
import io
import json
import re
import struct
from collections import Counter
from pathlib import Path

from reading_words import ReadingWords, OVERRIDES
from text_pairs import apply_reviewed_pairs
from word_card_edits import EDITS, apply_edits, dictionary_pitch, read_edits

ROOT = Path(__file__).resolve().parents[1]
CORPUS = ROOT / 'corpus'
OUTPUT = ROOT / 'package/mahoyo-learning/local'
RUBY = re.compile(r'<([^<>|]+)\|([^<>]+)>')
CONTROL = re.compile(r'@o|\^')
LATIN = re.compile(r'[A-Za-z]')
HAN = re.compile(r'[\u3400-\u9fff]')
KANA = re.compile(r'^[ぁ-ヿー]*$')
# Sudachi splits 草十郎 as 草 + 十郎. Unannotated occurrences need the same
# protection as names with native ruby. Names are kept visible as authored.
NAMES = re.compile('草十郎|青子|有珠|橙子|静希|蒼崎|久遠寺|槻司|鳶丸|芳助|久万梨|金鹿|周瀬|唯架|律架|文柄|詠梨|社木|三咲')



def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def decode_japanese(source):
    """Collect ruby spans on the same visible text that native/plain() uses."""
    parts, ruby = [], []
    cursor = offset = 0
    for match in RUBY.finditer(source):
        prefix = CONTROL.sub('', source[cursor:match.start()])
        base = CONTROL.sub('', match[1])
        reading = re.sub(r'@p-?\d+/|@_', '', match[2])
        parts.extend([prefix, base])
        offset += len(prefix)
        ruby.append((offset, offset + len(base), base, reading))
        offset += len(base)
        cursor = match.end()
    parts.append(CONTROL.sub('', source[cursor:]))
    return ''.join(parts), ruby


def chinese_caption(source):
    # In incantations the translation is the annotation above the Latin text.
    # The original translator sometimes leaves the final Latin letter outside
    # the tag for typesetting. It belongs only to the Japanese body.
    def caption(match):
        base, reading, suffix = match.groups()
        if LATIN.search(base) and not HAN.search(base) and HAN.search(reading):
            return reading
        return base + suffix
    pattern = r'<([^<>|]+)\|([^<>]+)>([A-Za-z]*)'
    return CONTROL.sub('', re.sub(pattern, caption, source))


def hira(text):
    return ''.join(chr(ord(c)-0x60) if 'ァ' <= c <= 'ヶ' else c for c in text)


def authored_reading(start, end, body, rubies, reading, definition):
    covered = [r for r in rubies if start <= r[0] and r[1] <= end and r[3].strip('・')]
    if not covered:
        return reading, definition
    cursor, pieces = start, []
    for left, right, base, kana in covered:
        gap = body[cursor:left]
        if not KANA.fullmatch(gap):
            break
        pieces.extend((gap, kana))
        cursor = right
    else:
        tail = body[cursor:end]
        if KANA.fullmatch(tail):
            author = ''.join(pieces) + tail
            if hira(author) != hira(reading):
                reading = f'{author}（原作） / {reading}（辞書）'
            return reading, definition
    return reading, definition + '\n原作注音：' + '、'.join(f'{b}（{r}）' for _, _, b, r in covered)


def dictionary_words(engine, notes, body, rubies, key):
    from sudachipy import tokenizer
    names = [match.span() for match in NAMES.finditer(body)]
    for token in engine.tokenize(body, tokenizer.Tokenizer.SplitMode.C):
        surface = token.surface()
        pos = token.part_of_speech()[0]
        if pos in {'助詞', '助動詞', '補助記号', '空白', '記号'}:
            continue
        lemma = token.dictionary_form()
        reading = hira(token.reading_form())
        # Exact dictionary forms keep homophones and inflected strings apart.
        note = notes.get(lemma)
        if not note or not note['definition_zh'].strip():
            continue
        if surface == lemma and hira(note['reading']) != reading:
            continue
        if surface != lemma and (pos not in {'動詞','形容詞'} or not HAN.search(lemma)):
            continue
        start, end = token.begin(), token.end()
        if any(start < right and left < end for left, right in names):
            continue
        if any(start < right and left < end and not (start <= left and right <= end)
               for left, right, _, _ in rubies):
            continue
        if body[start:end] != surface:
            raise ValueError(f'Tokenizer offset mismatch at {key}')
        meaning = note['definition_zh']
        if key == 1096 and lemma == 'もくもく':
            meaning = '默默地，一声不响地（吃着便当）。'
        if surface != lemma:
            meaning = f'辞書形：{lemma}（{note["reading"]}）\n' + meaning
        reading, meaning = authored_reading(start, end, body, rubies, reading, meaning)
        yield (start, end, surface, reading, note['part_of_speech'], meaning,
               '/'.join(note['levels']), note['pitch'] if surface == lemma else '')


def words_for(engine, notes, source, key, supplemental=None, edits=(), notes_by_id=None):
    body, rubies = decode_japanese(source)
    words = list(dictionary_words(engine, notes, body, rubies, key))
    for start, end, term, reading in rubies:
        if term == '幼馴染' and body[end:end+1] == 'み':
            term += 'み'
            reading += 'み'
            end += 1
        if reading.strip('・'):
            meaning = supplemental.original_meaning(term) if supplemental else ''
            words.append((start, end, term, reading, '原作注音', meaning, '原作', ''))
        else:
            words.append((start, end, term, '', '原作傍点', '原作用傍点强调此处。', '原作', ''))
    if edits:
        apply_edits(body, words, edits, notes_by_id, authored_reading, rubies)
    if supplemental:
        words.extend(supplemental.words(body, rubies, key, words))
    return body, words


def write_string(out, value):
    raw = value.encode('utf-16le')
    out.write(struct.pack('<I', len(raw)//2))
    out.write(raw)


def load_sources(*, by_id=False):
    manifest = json.loads((CORPUS/'manifest.json').read_text('utf8'))
    contents = {}
    for name, expected in manifest['files'].items():
        raw = (CORPUS/name).read_bytes()
        if sha(raw) != expected:
            raise ValueError('Source hash mismatch: ' + name)
        contents[name] = raw.decode('utf-8-sig')
    ja = contents['steam-ja.ctd'].splitlines()
    zh = contents['sumisora-steam-zh.txt'].splitlines()
    apply_reviewed_pairs(ja, zh, ROOT/'data-prep/reviewed-text-pairs.json', manifest['translation_commit'])
    notes = json.loads(contents['anki-jlpt-n1-n2.json'])
    if len(ja) != len(zh) or len(ja) != manifest['native_text_keys'] or len(notes) != manifest['jlpt_note_count']:
        raise ValueError('Source cardinality mismatch')
    return manifest, ja, zh, {n['note_id' if by_id else 'term']: n for n in notes}


def build():
    from sudachipy import dictionary
    manifest, ja, zh, notes_by_id = load_sources(by_id=True)
    notes = {n['term']: n for n in notes_by_id.values()}
    edits = read_edits()
    if any(not 0 <= key < len(ja) for key in edits):
        raise ValueError('Reviewed word card edit references an invalid story key')
    lexicon = dictionary.Dictionary(dict='core')
    engine = lexicon.create()
    supplemental = ReadingWords(lexicon, engine, notes)
    out = io.BytesIO()
    out.write(b'MHJL0002')
    out.write(struct.pack('<I', len(ja)))
    counts, terms = Counter(), set()
    supplemental_definitions = supplemental_lemmas = 0
    for key, (source, translated) in enumerate(zip(ja, zh)):
        body, words = words_for(engine, notes, source, key, supplemental, edits.get(key, ()), notes_by_id)
        caption = chinese_caption(translated)
        if not body or not caption or any(c in caption for c in '<>|^@\x00'):
            raise ValueError(f'Invalid markup or empty caption at {key}: {caption!r}')
        out.write(struct.pack('<I', key))
        write_string(out, source)
        write_string(out, caption)
        out.write(struct.pack('<I', len(words)))
        for start, end, surface, reading, pos, definition, level, pitch in words:
            if body[start:end] != surface:
                raise ValueError(f'Invalid span at {key}: {surface}')
            out.write(struct.pack('<II', len(body[:start].encode('utf-16le'))//2,
                                  len(body[:end].encode('utf-16le'))//2))
            for value in (surface, reading, pos, definition, level, dictionary_pitch(reading, pitch)):
                write_string(out, value)
            counts[level] += 1
            if level == '补注':
                lines = definition.splitlines()
                if any(line and not line.startswith(('原作注音：','辞書形：')) for line in lines):
                    supplemental_definitions += 1
                if any(line.startswith('辞書形：') for line in lines):
                    supplemental_lemmas += 1
            if level != '原作':
                terms.add(surface)
    raw = out.getvalue()
    report = dict(format='MHJL0002', scope='full-game', records=len(ja),
                  range=[0,len(ja)-1], bytes=len(raw), sha256=sha(raw),
                  pairing_sha256=sha((ROOT/"data-prep/reviewed-text-pairs.json").read_bytes()),
                  reading_overrides_sha256=sha(OVERRIDES.read_bytes()),
                  reviewed_word_cards_sha256=sha(EDITS.read_bytes()),
                  reviewed_word_cards=sum(map(len,edits.values())),
                  source_hashes=manifest['files'], translation_commit=manifest['translation_commit'],
                  private_local_data=False, distribution_scope='with-mahoyo-yomibito',
                  annotations=dict(counts), distinct_study_surfaces=len(terms),
                  supplemental_definitions=supplemental_definitions, supplemental_lemmas=supplemental_lemmas)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT/'story.dat').write_bytes(raw)
    (OUTPUT/'provenance.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n','utf8')
    print(json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    build()
