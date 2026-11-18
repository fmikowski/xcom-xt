# SPDX-License-Identifier: GPL-3.0-or-later
"""BENCH.TXT of several builds side by side (tools\\cpubench.ps1 writes them).

    python tools/benchcmp.py build/cpubench vfa1 vju7 -- 86 186 286 [--md]

Reads <dir>/<case>-<build>/BENCH.TXT and LOG.TXT. Prints, per case, every BENCH
line with a time ("n ms"), one column a build (several times on a line: a/b),
T1's alien turn from the LOG, then the checks: any "n ... differ" with n > 0,
"DIFFERENT", and whether every build drew the same pictures (the "view ...
picture xxxxxxxx" hashes; the 8086 build's are the reference).
"""
import os
import re
import sys

MS = re.compile(r'(\d+) ms')


def read(path):
    try:
        with open(path, encoding='latin-1') as f:
            return [l.rstrip('\r\n') for l in f]
    except OSError:
        return None


def main(argv):
    md = '--md' in argv
    argv = [a for a in argv if a != '--md']
    d = argv[0]
    i = argv.index('--')
    cases, builds = argv[1:i], argv[i + 1:]
    bad = 0
    for c in cases:
        rows = {}       # key -> {build: "a/b"}
        order = []
        pics = {}       # build -> [hashes]
        checks = []
        for b in builds:
            bench = read(os.path.join(d, f'{c}-{b}', 'BENCH.TXT'))
            log = read(os.path.join(d, f'{c}-{b}', 'LOG.TXT'))
            if bench is None and log is None:
                checks.append(f'{b}: no BENCH.TXT or LOG.TXT')
                bad += 1
                continue
            bench = bench or []
            log = log or []
            for l in bench:
                m = re.search(r'picture ([0-9a-f]{8})', l)
                if m:
                    pics.setdefault(b, []).append(m.group(1))
                    l = l[:m.start()].rstrip(', ')
                if re.search(r'\b([1-9]\d*) (screen bytes |pixels |cells of lit tiles dark|)\w* ?differ|DIFFERENT|DARK', l):
                    checks.append(f'{b}: {l}')
                    bad += 1
                if l.startswith('VL '):
                    continue
                v = MS.findall(l)
                if not v:
                    continue
                k = MS.sub('# ms', l)
                k = re.sub(r', \d+ reads', '', k)
                if k not in rows:
                    rows[k] = {}
                    order.append(k)
                rows[k][b] = '/'.join(v)
            for l in log:
                for pat, k in ((r'T1: alien turn (\d+) ms', 'T1: alien turn # ms (LOG)'),
                               (r'INTRO (\d+) of \d+ frames shown, (\d+) late \(max (\d+) ms',
                                'INTRO # frames shown, # late (max # ms) (LOG)'),
                               (r'held item: \d+ moves, \d+ with the picture, (\d+\.\d+) ms a move',
                                'held item: # ms a move (LOG)')):
                    m = re.match(pat, l)
                    if m:
                        if k not in rows:
                            rows[k] = {}
                            order.append(k)
                        rows[k][b] = '/'.join(m.groups())
        ref = pics.get(builds[0])
        for b in builds[1:]:
            if b in pics and ref is not None and pics[b] != ref:
                checks.append(f'{b}: view pictures differ from {builds[0]}\'s')
                bad += 1
        print(f'\n== {c}')
        if md:
            print('| ' + ' | '.join(['line'] + builds) + ' |')
            print('|' + '---|' * (len(builds) + 1))
            for k in order:
                print('| ' + ' | '.join([k] + [rows[k].get(b, '-') for b in builds]) + ' |')
        else:
            w = max([len(k) for k in order] + [10])
            print(' ' * w + ''.join(f'{b:>12}' for b in builds))
            for k in order:
                print(f'{k:<{w}}' + ''.join(f'{rows[k].get(b, "-"):>12}' for b in builds))
        n = len(ref) if ref else 0
        print(f'view pictures: {n} per build, ' + ('all the same' if not any('pictures differ' in x for x in checks) else 'DIFFER'))
        for x in checks:
            print('CHECK ' + x)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
