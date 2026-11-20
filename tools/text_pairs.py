"""Apply verified source-table pairings and caption edits for the builder and text lookup."""
import json


def apply_reviewed_pairs(ja, zh, path, translation_commit):
    corrections = json.loads(path.read_text('utf8'))
    if corrections['translation_commit'] != translation_commit:
        raise ValueError('Translation corrections target a different revision')
    used = set()
    for swap in corrections['swaps']:
        left, right = swap['keys']
        if not (0 <= left < len(ja) and 0 <= right < len(ja)):
            raise ValueError('Translation pairing outside source table')
        if left == right or used.intersection((left, right)) or [ja[left], ja[right]] != swap['expected_japanese']:
            raise ValueError('Stale or overlapping translation pairing')
        used.update((left, right))
        zh[left], zh[right] = zh[right], zh[left]

    edited = set()
    for entry in corrections['caption_edits']:
        key = entry['key']
        if key in edited or not 0 <= key < len(ja):
            raise ValueError(f'Duplicate or out-of-range caption edit: {key}')
        if ja[key] != entry['expected_japanese'] or zh[key] != entry['expected_chinese']:
            raise ValueError(f'Caption edit targets different source text: {key}')
        if not entry['replacement'].strip():
            raise ValueError(f'Empty caption edit: {key}')
        zh[key] = entry['replacement']
        edited.add(key)
