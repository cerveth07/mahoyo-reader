"""Apply approved B-group decisions by pinned story key and UTF-16 span."""
import json
import re
from collections import defaultdict
from pathlib import Path

EDITS = Path(__file__).resolve().parents[1] / 'data-prep/reviewed-word-cards.jsonl'


def read_edits():
    edits = defaultdict(list)
    selected = set()
    with EDITS.open(encoding='utf8') as stream:
        for line in stream:
            edit = json.loads(line)
            signature = edit['key'], tuple(edit['span'])
            if signature in selected:
                raise ValueError(f'Duplicate word card edit: {signature}')
            selected.add(signature)
            edits[edit['key']].append(edit)
    return edits


def dictionary_pitch(reading, pitch):
    """Mark the dictionary accent explicitly when author and lexical readings differ."""
    if pitch and '（原作） / ' in reading and reading.endswith('（辞書）'):
        return '辞书重音 ' + pitch
    return pitch


def apply_edits(body, words, edits, notes_by_id, authored_reading, rubies):
    units = body.encode('utf-16le')
    original = list(words)
    for edit in edits:
        left, right = edit['span']
        surface = units[left*2:right*2].decode('utf-16le')
        if not (0 <= left < right <= len(units)//2 and surface == edit['surface']):
            raise ValueError(f'Invalid reviewed span: {edit}')
        start = len(units[:left*2].decode('utf-16le'))
        end = start + len(surface)
        note = notes_by_id[edit['note_id']]
        reading = edit.get('reading', note['reading'])
        lemma = re.sub(r'\[[^\]]+\]', '', note['term'])
        meaning = edit.get('meaning', note['definition_zh'])
        pos = edit.get('pos', note['part_of_speech'])
        if not reading or not meaning or not note['levels']:
            raise ValueError(f'Missing reviewed card information: {edit}')
        preserve_pitch = reading == note['reading']
        if not preserve_pitch or surface != lemma:
            meaning = f'辞書形：{lemma}（{note["reading"]}）\n' + meaning
        reading, meaning = authored_reading(start, end, body, rubies, reading, meaning)
        if 'old' in edit:
            old_left, old_right, old_surface, old_reading, old_level = edit['old']
            expected_start = len(units[:old_left*2].decode('utf-16le'))
            expected_end = len(units[:old_right*2].decode('utf-16le'))
            matches = [w for w in original if w[:4] == (expected_start, expected_end, old_surface, old_reading)
                       and w[6] == old_level and old_level != '原作']
            if len(matches) != 1 or matches[0] not in words:
                raise ValueError(f'Review target no longer matches: {edit}')
            words.remove(matches[0])
        if any(start < w[1] and w[0] < end and w[6] != '原作' for w in words):
            raise ValueError(f'Reviewed span overlaps an existing learning card: {edit}')
        pitch = note['pitch'] if preserve_pitch else ''
        words.append((start, end, surface, reading, pos, meaning, '/'.join(note['levels']), pitch))
