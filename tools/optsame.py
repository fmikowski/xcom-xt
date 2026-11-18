# SPDX-License-Identifier: GPL-3.0-or-later
"""The verdict of tools\\optsame.ps1: did two programs behave the same?

    python tools/optsame.py <dir> [--json out.json] [--keep n] -- old new

Reads <dir>/<case>-old/ and <dir>/<case>-new/ (LOG.TXT, BENCH.TXT, MAPDUMP.BIN, *.SAV,
XCOMXT.CFG, shot.png) for every case of tools/optcases.json that was run and prints SAME
or DIFFERENT per case, with what differs:

  log      LOG.TXT line by line, after masking what speed and code size may change:
           times ("123 ms", the time a PACE line took, the AI's profile, the AdLib's
           measured timing), disk reads ("5 reads, 11 KB"), free memory figures, the
           address of the read cache's stub and the size of the Hercules copier's code.
           Everything else must be equal: every move, shot, roll, state check sum and
           message, and what fitted into memory (list sizes, pictures loaded or left out).
  bench    BENCH.TXT the same way: the pictures' check sums ("picture xxxxxxxx"), the
           "n differ" checks, counts and the voxel lines stay; times are masked.
  pictures the "picture xxxxxxxx" check sums of BENCH.TXT alone (reported besides bench)
  screen   the end screenshot, the emulated screen only (the window's frame cut off),
           pixel by pixel
  saves    every *.SAV, XCOMXT.CFG and MAPDUMP.BIN byte for byte, and the same set of files
           (not for a case with "screen": false: its end screen is animated by the clock)
  ran      both runs reached their last expected line (run.txt)

Exit code 0 if every case is SAME, 1 otherwise.
"""
import json
import os
import re
import sys

STAMP = re.compile(r'^@[0-9A-F]{8} ')
MASK = [
    (re.compile(r'\d+(\.\d+)? ms\b'), 'N ms'),
    (re.compile(r'\d+ reads, \d+ KB'), 'N reads, N KB'),
    (re.compile(r', \d+ reads\b'), ', N reads'),
    (re.compile(r'^(reads before the battle): \d+, \d+ KB'), r'\1: N, N KB'),
    (re.compile(r'^(PACE \w \d+) \d+$'), r'\1 N'),
    (re.compile(r'^(LIM:) \d+ (paragraphs free at the start)'), r'\1 N \2'),
    (re.compile(r'^(DOS free at start:) \d+ KB'), r'\1 N KB'),
    (re.compile(r'^memory: program \d+ KB \(\d+ KB of near heap given back\), \d+ KB free'), 'memory: program N'),
    (re.compile(r'free( memory)? \d+ KB \(\d+ KB in all(, \d+ paragraphs)?\)'), 'free N KB'),
    (re.compile(r'free( memory)? \d+ KB'), 'free N KB'),
    (re.compile(r'\d+ KB free\b'), 'N KB free'),
    (re.compile(r'(T\d+: ai ms:).*'), r'\1 N'),
    (re.compile(r'stub at [0-9A-F]{4}'), 'stub at N'),              # the read cache's stub: where the program ends
    (re.compile(r'\(copier \d+ bytes\)'), '(copier N bytes)'),      # Hercules: the size of its code
    (re.compile(r'^(music: OPL2), .*'), r'\1 N'),                   # the AdLib's timing, measured at the start
    (re.compile(r'(cost \d+ dijkstra \d+[^,]*), \d+ / N ms'), r'\1, N / N ms'),
    (re.compile(r'(canvas: .*kept:) \d+ / N ms'), r'\1 N / N ms'),  # a time without its unit
]


def read(path):
    try:
        with open(path, encoding='latin-1', newline='') as f:
            return [l.rstrip('\r\n') for l in f]
    except OSError:
        return None


def norm(lines):
    out = []
    for l in lines:
        l = STAMP.sub('', l)
        for pat, rep in MASK:
            l = pat.sub(rep, l)
        out.append(l)
    return out


def first_diff(a, b):
    for i in range(max(len(a), len(b))):
        la = a[i] if i < len(a) else '(end)'
        lb = b[i] if i < len(b) else '(end)'
        if la != lb:
            return f'line {i + 1} of {len(a)} / {len(b)}: "{la}" | "{lb}"'
    return None


def screen(path):
    """The emulated screen of an 86Box window shot: the frame, menu, tool bar and status
    bar cut off (the tool bar shows the speed in percent, the status bar disk lights)."""
    from PIL import Image
    im = Image.open(path).convert('RGB')
    w, h = im.size
    return im.crop((3, 73, w - 3, h - 25))


def cmp_screen(pa, pb, time_shown=False):
    """time_shown: BENCH ends on a screen that shows its average redraw time in digits
    ("Avg full redraw N ms"). When the two programs' averages differ, a difference that
    lies inside those digits (one text row, at most 5 digits wide) is the time, not the
    picture."""
    if not os.path.exists(pa) or not os.path.exists(pb):
        return 'no screenshot'
    from PIL import ImageChops
    a, b = screen(pa), screen(pb)
    if a.size != b.size:
        return f'sizes {a.size} / {b.size}'
    box = ImageChops.difference(a, b).getbbox()
    if box and time_shown and box[3] - box[1] <= 20 and box[2] - box[0] <= 100:
        return None
    if box:
        return f'pixels differ in {box}'
    return None


def main(argv):
    out_json = None
    if '--json' in argv:
        out_json = argv[argv.index('--json') + 1]
        del argv[argv.index('--json'):argv.index('--json') + 2]
    d = argv[0]
    old, new = argv[argv.index('--') + 1:][:2]
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, 'optcases.json'), encoding='utf-8') as f:
        cases = json.load(f)['cases']
    res = []
    for c in cases:
        da, db = (os.path.join(d, f"{c['name']}-{x}") for x in (old, new))
        if not os.path.isdir(da) and not os.path.isdir(db):
            continue
        why = []
        for x, p in ((old, da), (new, db)):
            r = read(os.path.join(p, 'run.txt'))
            if not r or 'Log matched' not in r[0]:
                why.append(f'ran: {x} did not reach its last line ({r[0] if r else "no run.txt"})')
        for what, name in (('log', 'LOG.TXT'), ('bench', 'BENCH.TXT')):
            a, b = read(os.path.join(da, name)), read(os.path.join(db, name))
            if a is None and b is None:
                if what == 'log':
                    why.append('log: no LOG.TXT')
                continue
            if a is None or b is None:
                why.append(f'{what}: only one {name}')
                continue
            if what == 'bench':
                pa = [m.group(1) for l in a for m in [re.search(r'picture ([0-9a-f]{8})', l)] if m]
                pb = [m.group(1) for l in b for m in [re.search(r'picture ([0-9a-f]{8})', l)] if m]
                if pa != pb:
                    why.append(f'pictures: {sum(1 for x, y in zip(pa, pb) if x != y) + abs(len(pa) - len(pb))} of {max(len(pa), len(pb))} check sums differ')
            df = first_diff(norm(a), norm(b))
            if df:
                why.append(f'{what}: {df}')
        avg = [[m.group(1) for l in (read(os.path.join(p, 'BENCH.TXT')) or [])
                for m in [re.match(r'average full redraw (\d+) ms', l)] if m] for p in (da, db)]
        # "screen": false in a case: its end screen shows something animated by the clock
        # (two runs of one program differ there), so the screenshot proves nothing.
        s = c.get('screen', True) and cmp_screen(os.path.join(da, 'shot.png'), os.path.join(db, 'shot.png'),
                                                 time_shown=bool(avg[0]) and avg[0] != avg[1])
        if s:
            why.append(f'screen: {s}')
        fa, fb = ({f.upper() for f in os.listdir(p) if re.search(r'\.(SAV|CFG|BIN)$', f, re.I)} if os.path.isdir(p) else set()
                  for p in (da, db))
        if fa != fb:
            why.append(f'saves: files {sorted(fa)} / {sorted(fb)}')
        for f in sorted(fa & fb):
            with open(os.path.join(da, f), 'rb') as x, open(os.path.join(db, f), 'rb') as y:
                ba, bb = x.read(), y.read()
            if ba != bb:
                at = next((i for i in range(min(len(ba), len(bb))) if ba[i] != bb[i]), min(len(ba), len(bb)))
                why.append(f'saves: {f} differs at byte {at} ({len(ba)} / {len(bb)} bytes)')
        res.append({'case': c['name'], 'same': not why, 'why': why, 'files': sorted(fa | fb)})
    w = max([len(r['case']) for r in res] + [4])
    for r in res:
        print(f"{'SAME     ' if r['same'] else 'DIFFERENT'} {r['case']:<{w}}" + (f"  ({len(r['files'])} files)" if r['same'] and r['files'] else ''))
        for x in r['why']:
            print(' ' * 10 + x[:300])
    n = sum(1 for r in res if not r['same'])
    print(f"{len(res) - n} of {len(res)} cases SAME" + (f', {n} DIFFERENT' if n else ''))
    if out_json:
        with open(out_json, 'w', encoding='utf-8') as f:
            json.dump({'old': old, 'new': new, 'cases': res}, f, indent=1)
    return 1 if n or not res else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
