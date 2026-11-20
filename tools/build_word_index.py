"""Index the visible script by dictionary form, reading, POS and UTF-16 word boundaries."""
import hashlib
import struct

from full_game import OUTPUT, decode_japanese, write_string, hira
from reading_words import ReadingWords, contextual_reading
from sudachipy import dictionary
from validate_full_game import read_story

SKIP = {'空白', '補助記号', '記号'}
ALIASES = {'うなずく': '頷く', '頷く': '頷く', '何げない': '何気ない', '何気無い': '何気ない'}


def build():
    raw = (OUTPUT / 'story.dat').read_bytes()
    lexicon = dictionary.Dictionary(dict='core')
    engine = lexicon.create()
    resolver = ReadingWords(lexicon, engine, {})
    entries, readings, rows = {}, {}, []
    def intern(mapping, value):
        return mapping.setdefault(value, len(mapping))
    for key, (ja, _, words) in enumerate(read_story(raw)):
        body, _ = decode_japanese(ja)
        offs = [0]
        for c in body:
            offs.append(offs[-1] + len(c.encode('utf-16le')) // 2)
        cards = {(w[0], w[1]): w for w in words if w[6] == '补注'}
        row = []
        for t in engine.tokenize(body):
            group = t.part_of_speech()[0]
            if group in SKIP:
                continue
            l, r = t.begin(), t.end()
            a, b = offs[l], offs[r]
            lemma = t.dictionary_form()
            kana = contextual_reading(body,l,r,lemma,hira(t.reading_form()))
            card = cards.get((a,b))
            if card and card[3] and all('ぁ' <= c <= 'ゖ' or c == 'ー' for c in card[3]):
                kana = card[3]
            base_kana = resolver.lemma_reading(t,kana) if group in {'動詞','形容詞'} and t.surface()!=lemma else kana
            canonical = ALIASES.get(lemma,lemma)
            idx = intern(entries,(canonical,base_kana,group))
            row.append((a,b,idx,intern(readings,kana)))
        rows.append(row)
    target = OUTPUT/'word-index.dat'
    with target.open('wb') as out:
        out.write(b'MHLEX001')
        write_string(out,hashlib.sha256(raw).hexdigest())
        out.write(struct.pack('<I',len(entries)))
        for entry in entries:
            for field in entry:
                write_string(out,field)
        out.write(struct.pack('<I',len(readings)))
        for reading in readings:
            write_string(out,reading)
        out.write(struct.pack('<I',len(rows)))
        for row in rows:
            out.write(struct.pack('<I',len(row)))
            for hit in row:
                out.write(struct.pack('<HHII',*hit))
    print(f'Word index: {len(rows)} keys, {len(entries)} lexical entries, {sum(map(len,rows))} positions')


if __name__=='__main__':
    build()
