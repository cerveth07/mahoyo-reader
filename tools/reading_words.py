"""Generate hover readings for the Han spans left by the existing study deck."""
import json
import re
from functools import lru_cache
from os.path import commonprefix
from pathlib import Path

HAN = re.compile(r'[\u3400-\u9fff\uf900-\ufaff\U00020000-\U000323af々]')
KANA = re.compile(r'[ぁ-ゖァ-ヺーゝゞヽヾ]+')
NUMBER = re.compile(r'[〇一二三四五六七八九十百千万億0-9０-９]+')
COUNTERS = {
    '人':'にん', '匹':'ひき', '本':'ほん', '杯':'はい', '羽':'わ', '個':'こ',
    '歳':'さい', '時':'じ', '分':'ふん', '秒':'びょう', '年':'ねん', '月':'がつ',
    '日':'にち', '階':'かい', '回':'かい', '歩':'ほ', '枚':'まい', '頭':'とう', '度':'ど', 'つ':'つ',
    '時間':'じかん', '週間':'しゅうかん', '件':'けん', '点':'てん', '周':'しゅう', '冊':'さつ',
    '軒':'けん', '品':'ひん', '片':'へん', '節':'せつ', '種':'しゅ', '世紀':'せいき',
    '分間':'ふんかん', '晩':'ばん', '組':'くみ', '振り':'ふり', '呼吸':'こきゅう', '言':'こと',
    '目':'め', '割':'わり', '名':'めい',
    'ヶ月':'かげつ', 'か月':'かげつ', 'カ月':'かげつ', 'ヵ月':'かげつ', '箇月':'かげつ',
}
MONTH_COUNTERS = {'ヶ月', 'か月', 'カ月', 'ヵ月', '箇月'}
SOKUON_COUNTERS = {
    '件','点','周','冊','軒','節','種','世紀','週間','個','階','回','歩','歳','頭', *MONTH_COUNTERS,
}
ROKU_SOKUON_COUNTERS = {'件','軒','個','階','回', *MONTH_COUNTERS}
VOICED_H_COUNTERS = {
    '匹':('ぴき','びき'), '本':('ぽん','ぼん'), '杯':('ぱい','ばい'),
    '品':('ぴん','びん'), '片':('ぺん','べん'),
}
COMPOUND_READINGS = {
    '何': {'何げない':('なにげない','形容词'), '何げなく':('なにげなく','副词')},
    '何気': {'何気ない':('なにげない','形容词'), '何気なく':('なにげなく','副词')},
    '一': {'一本道':('いっぽんみち','名词')},
    '一言': {'一言一句':('いちごんいっく','名词')},
}
DIGITS = '〇一二三四五六七八九'
UNIT_FORMS = {
    '人': {1:'ひとり', 2:'ふたり', 4:'よにん'},
    '歳': {20:'はたち'}, '時': {4:'よじ', 7:'しちじ', 9:'くじ'},
    '晩': {1:'ひとばん', 2:'ふたばん'}, '組': {1:'ひとくみ'},
    '振り': {1:'ひとふり'}, '呼吸': {1:'ひとこきゅう'}, '言': {1:'ひとこと'},
    '目': {1:'ひとめ'},
    '月': {4:'しがつ', 7:'しちがつ', 9:'くがつ'},
    '日': {2:'ふつか', 3:'みっか', 4:'よっか', 5:'いつか', 6:'むいか',
          7:'なのか', 8:'ようか', 9:'ここのか', 10:'とおか',
          14:'じゅうよっか', 20:'はつか', 24:'にじゅうよっか'},
    'つ': dict(enumerate(('ひとつ','ふたつ','みっつ','よっつ','いつつ','むっつ','ななつ','やっつ','ここのつ'), 1)),
}
GEMINATE = {'いち':'いっ', 'ろく':'ろっ', 'はち':'はっ', 'じゅう':'じゅっ', 'ひゃく':'ひゃっ'}
POS_NAMES = {
    '名詞':'名词', '動詞':'动词', '形容詞':'形容词', '形状詞':'形容动词', '代名詞':'代词',
    '副詞':'副词', '接頭辞':'接头词', '接尾辞':'接尾词', '接続詞':'接续词',
    '連体詞':'连体词', '感動詞':'感叹词', '助詞':'助词', '助動詞':'助动词',
}
OVERRIDES = Path(__file__).resolve().parents[1] / 'data-prep/reading-overrides.json'


def hiragana(text):
    return ''.join(chr(ord(c)-0x60) if 'ァ' <= c <= 'ヶ' else c for c in text)


def kana_only(text):
    return bool(KANA.fullmatch(re.sub(r'[\s・、。]', '', text)))


def numeral_value(text):
    if text.isdecimal():
        return int(text)
    if text[-1] in '万億' and text[:-1].isdecimal():
        return int(text[:-1]) * (10_000 if text[-1] == '万' else 100_000_000)
    total = group = digit = 0
    for ch in text:
        if ch in DIGITS:
            digit = DIGITS.index(ch)
        elif ch in '十百千':
            group += (digit or 1) * 10**('十百千'.index(ch)+1)
            digit = 0
        else:
            total += (group + digit or 1) * (10_000 if ch == '万' else 100_000_000)
            group = digit = 0
    return total + group + digit


def numeral_spelling(value):
    if value == 0:
        return '〇'
    parts = []
    for unit, size in (('億', 100_000_000), ('万', 10_000), ('千', 1000), ('百', 100), ('十', 10)):
        amount, value = divmod(value, size)
        if amount:
            prefix = '' if amount == 1 and size < 10_000 else numeral_spelling(amount)
            parts.append(prefix + unit)
    if value:
        parts.append(DIGITS[value])
    return ''.join(parts)


def counter_reading(base, reading, unit, preceding):
    value = numeral_value(base)
    if unit == '日' and value == 1 and preceding.endswith('月'):
        return 'ついたち'
    if value in UNIT_FORMS.get(unit, {}):
        return UNIT_FORMS[unit][value]
    if unit == '月' and value > 12:
        return None
    ending = next((e for e in GEMINATE if reading.endswith(e)), '')
    if unit in VOICED_H_COUNTERS:
        after_stop, after_n = VOICED_H_COUNTERS[unit]
        if ending:
            return reading[:-len(ending)] + GEMINATE[ending] + after_stop
        if unit in {'匹','本','杯'} and reading.endswith(('さん','せん','まん')):
            return reading + after_n
    if unit in {'分','分間'}:
        tail = 'かん' if unit == '分間' else ''
        if ending:
            return reading[:-len(ending)] + GEMINATE[ending] + 'ぷん' + tail
        if reading.endswith(('さん','よん','せん','まん')):
            return reading + 'ぷん' + tail
    if unit in SOKUON_COUNTERS and ending and (ending != 'ろく' or unit in ROKU_SOKUON_COUNTERS):
        return reading[:-len(ending)] + GEMINATE[ending] + ('ぽ' if unit == '歩' else COUNTERS[unit])
    if reading.endswith('さん') and unit in {'階','歩','軒'}:
        return reading + {'階':'がい','歩':'ぽ','軒':'げん'}[unit]
    return reading + COUNTERS[unit]


def standard_verb_reading(lemma, reading):
    # Normalize the conventional spelling of this verb, including forms such as 言うて.
    return 'い' + reading[1:] if lemma == '言う' and reading.startswith('ゆ') else reading


def contextual_reading(body, left, right, lemma, reading):
    word, following = body[left:right], body[right:]
    if word == '私' and reading == 'わたくし':
        return 'わたし'
    if word == '何' and (following[:1] in {'か','も','を','が','に','へ'} or following.startswith('より')):
        return 'なに'
    if word == '他' and reading == 'た':
        formal = following.startswith(('を圧する','の追随を許さない'))
        listed = body[max(0,left-2):left] == 'その' and not following.startswith('に')
        if not formal and not listed:
            return 'ほか'
    if word == '一日':
        return 'ついたち' if body[:left].endswith('月') else 'いちにち'
    if word == '一目' and following.startswith(('も見','を見','で見')):
        return 'ひとめ'
    return standard_verb_reading(lemma, reading)


class ReadingWords:
    def __init__(self, lexicon, engine, notes):
        self.lexicon, self.engine, self.notes = lexicon, engine, notes
        data = json.loads(OVERRIDES.read_text('utf8'))
        self.entries = {term:(reading,'名词 · 人名','') for term,reading in data['names'].items()}
        self.entries.update({term:(reading,'',meaning) for term,(reading,meaning) in data['words'].items()})
        self.by_key = {int(key):terms for key,terms in data['by_key'].items()}
        self.common = self.pattern(self.entries)

    @staticmethod
    def pattern(entries):
        return re.compile('|'.join(re.escape(word) for word in sorted(entries,key=lambda w:(-len(w),w))))

    def words(self, body, rubies, key, legacy):
        covered = set()
        for start,end,_term,reading,_pos,_meaning,level,_pitch in legacy:
            if (level != '原作' and KANA.search(reading)) or kana_only(reading):
                covered.update(range(start,end))
        needed = {m.start() for m in HAN.finditer(body)} - covered
        if not needed:
            return
        local = self.by_key.get(key)
        overrides = self.entries if local is None else self.entries | {
            word:(reading,'',meaning) for word,(reading,meaning) in local.items()
        }
        pattern = self.common if local is None else self.pattern(overrides)
        cursor = 0
        for match in pattern.finditer(body):
            start,end = match.span()
            yield from self.segment(body, cursor, start, needed, rubies)
            if emitted := self.card(body, start, end, *overrides[match[0]], needed, rubies):
                yield emitted
            cursor = end
        yield from self.segment(body, cursor, len(body), needed, rubies)
        if needed:
            raise ValueError(f'Unresolved Han readings at key {key}, offsets {sorted(needed)}: {body!r}')

    def original_meaning(self, term):
        return self.entries[term][2] if term in self.entries else ''

    def kana_number(self, term):
        if term.isdecimal() or re.fullmatch(r'[0-9０-９]+[万億]', term):
            term = numeral_spelling(numeral_value(term))
        return ''.join(hiragana(t.reading_form()) for t in self.engine.tokenize(term))

    @lru_cache(maxsize=4096)
    def dictionary_readings(self, lemma, pos):
        return tuple(dict.fromkeys(
            standard_verb_reading(lemma, hiragana(entry.reading_form()))
            for entry in self.lexicon.lookup(lemma)
            if entry.part_of_speech()[:5] == pos and entry.dictionary_form() == lemma
        ))

    def lemma_reading(self, token, reading):
        choices = self.dictionary_readings(token.dictionary_form(), token.part_of_speech()[:5])
        choices = [choice for choice in choices if KANA.fullmatch(choice)]
        if not choices:
            return ''
        scored = [(len(commonprefix((reading, choice))), choice) for choice in choices]
        best = max(score for score, _ in scored)
        matching = [choice for score, choice in scored if score == best]
        return matching[0] if len(matching) == 1 else ''

    def segment(self, body, start, end, needed, rubies):
        if not any(start <= i < end for i in needed):
            return
        tokens = self.engine.tokenize(body[start:end])
        ends = {start+token.end(): i+1 for i, token in enumerate(tokens)}
        index = 0
        while index < len(tokens):
            token = tokens[index]
            left,right = start+token.begin(),start+token.end()
            surface, lemma = body[left:right], token.dictionary_form()
            group,detail = token.part_of_speech()[:2]
            pos = '数词' if detail == '数詞' else POS_NAMES.get(group, '')
            reading = contextual_reading(body,left,right,lemma,hiragana(token.reading_form()))
            next_index = index+1
            meaning = ''

            for compound, (compound_reading, compound_pos) in COMPOUND_READINGS.get(surface, {}).items():
                compound_end = left+len(compound)
                if compound_end in ends and body[left:compound_end] == compound:
                    right, reading, pos = compound_end, compound_reading, compound_pos
                    next_index = ends[right]
                    if compound.endswith('なく'):
                        base = compound[:-1] + 'い'
                        meaning = f'辞書形：{base}（なにげない）'
                    break

            if detail == '数詞' and NUMBER.fullmatch(surface) and right == start+token.end():
                reading = self.kana_number(surface) if re.search(r'[0-9０-９]',surface) else reading
                if next_index < len(tokens):
                    following = tokens[next_index]
                    unit = following.surface()
                    unit_end = start+following.end()
                    if next_index+1 < len(tokens):
                        longer = unit + tokens[next_index+1].surface()
                        if longer in COUNTERS:
                            unit, unit_end = longer, start+tokens[next_index+1].end()
                    if unit == '分':
                        fraction = re.match(r'の[〇一二三四五六七八九十0-9０-９]', body[unit_end:])
                        minutes = hiragana(following.reading_form()) in {'ふん','ぷん'} or body[unit_end:].startswith('一秒')
                        if fraction or not minutes:
                            unit = ''
                    if unit in COUNTERS and not (unit == 'つ' and re.search('[万億十百千]',surface)):
                        combined = counter_reading(surface,self.kana_number(surface),unit,body[:left])
                        if combined:
                            right, reading, pos = unit_end, combined, '数量'
                            next_index = ends[right]

            if right == start+token.end():
                inflected = group in {'動詞','形容詞'} and surface != lemma
                base_reading = self.lemma_reading(token, reading) if inflected else reading
                note = self.notes.get(lemma)
                if note and base_reading and standard_verb_reading(lemma, hiragana(note['reading'])) == base_reading:
                    meaning = note['definition_zh'].strip()
                if inflected:
                    heading = f'辞書形：{lemma}' + (f'（{base_reading}）' if base_reading else '')
                    meaning = '\n'.join(filter(None,(heading,meaning)))

            if card := self.card(body,left,right,reading,pos,meaning,needed,rubies):
                yield card
            index = next_index

    @staticmethod
    def card(body, start, end, reading, pos, meaning, needed, rubies):
        hit = needed.intersection(range(start,end))
        if not hit or not kana_only(reading):
            return None
        needed.difference_update(hit)
        author = []
        for left,right,base,gloss in rubies:
            if start < right and left < end and gloss.strip('・') and (
                (left,right)!=(start,end) or hiragana(gloss) != reading
            ):
                author.append(f'{base}（{gloss}）')
        if author:
            meaning = '\n'.join(filter(None,(meaning,'原作注音：'+'、'.join(author))))
        return (start,end,body[start:end],reading,pos,meaning,'补注','')
