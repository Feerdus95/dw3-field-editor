"""Writes src/core/game_codes.inc from the decompilation's C: where the story
moves on (GAME.progress set at an event's end) and the flags the game's code
sets and checks outside the stage tables (the dump has those).

    python tools/game_codes.py [DECOMP]   (default: ../dw3_decomp)
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
SRC = Path(sys.argv[1] if len(sys.argv) > 1 else HERE.parent / 'dw3_decomp') / 'src'
NAMED = {'FIELD_PROGRESS_EXTRA': 0x2D, 'FIELD_PROGRESS_MOVIE_BATTLES': 0x2B}

FUNC = re.compile(r'^\w[\w\s\*]*?\b(\w+)\s*\([^;]*\)\s*\{')
PROGRESS = re.compile(r'GAME\.progress\s*=\s*([^;=]+);')
CALL = re.compile(r'(applyAction|checkCondition)\(\s*(?:FLAG\(\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*\)'
                  r'|(0x[0-9A-Fa-f]+|\d+))\s*,\s*(\d+)\s*\)')


def event_of(func):
    m = re.match(r'endEvents?(\d+)', func)
    return int(m.group(1)) if m else -1


story, flags = [], set()
for f in sorted(SRC.rglob('*.c')):
    lines = f.read_text(encoding='utf-8', errors='replace').split('\n')
    stage = f.stem if f.stem.startswith('wstag') else ''
    func = ''
    for i, l in enumerate(lines):
        m = FUNC.match(l)
        if m: func = m.group(1)
        m = PROGRESS.search(l)
        if m and stage:
            v = m.group(1).strip()
            point = NAMED.get(v) if v in NAMED else int(v, 0)
            j = i
            while j > 0 and '/*' not in lines[j]: j -= 1
            k = j
            while '*/' not in lines[k]: k += 1
            note = ' '.join(x.strip(' /*') for x in lines[j:k + 1])
            who = re.search(r'with ([^:]+?)(?::| moves| gives|$)', note)
            who = who.group(1).strip(' ,') if who else ''
            gift = re.search(r'gives the player (the [^.]+?)(?: and moves|$|\.)', note)
            story.append((point, stage, event_of(func), '' if who == 'the player' else who, gift.group(1).strip() if gift else ''))
        for m in CALL.finditer(l):
            code = int(m.group(2), 0) << 8 | int(m.group(3), 0) if m.group(2) else int(m.group(4), 0)
            if code < 0x4100:
                flags.add((code, m.group(1) == 'applyAction', int(m.group(5)), stage, event_of(func)))


def c(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


out = ['/* Written by tools/game_codes.py from the decompilation: do not edit */',
       '/* where the story moves on: its point, the stage, the event whose end does it, who is in it, what it gives */',
       'static const StoryStep STORY_STEPS[] = {']
out += ['    {0x%02X, %s, %d, %s, %s},' % (p, c(s), e, c(w), c(g)) for p, s, e, w, g in sorted(story)]
out += ['};', '/* the flags the game\'s code sets (set) or checks: the stage ("" for the field\'s own), the event whose end does it or -1 */',
        'static const GameFlagUse GAME_FLAG_USES[] = {']
out += ['    {0x%04X, %d, %d, %s, %d},' % (code, s, v, c(st), e) for code, s, v, st, e in sorted(flags)]
out += ['};', '']
(HERE / 'src/core/game_codes.inc').write_text('\n'.join(out), encoding='utf-8', newline='\n')
print(len(story), 'story steps,', len(flags), 'flag uses')
