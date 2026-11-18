# SPDX-License-Identifier: GPL-3.0-or-later
"""The table of tools\\optbench.ps1: one time in ms per metric and program.

    python tools/optbench.py <dir> [--json out.json] [--md] [--runs] -- name [name]

Reads <dir>/r<k>/<case>-<name>/LOG.TXT and BENCH.TXT (k = 1 .. the repeats) and
the case table tools/optcases.json. A metric of a case is one of

  {"name", "see", "bench": regex}   the regex's first group on a BENCH.TXT line: the
                                    game's own timer (ms)
  {"name", "see", "log": regex}     the same on a LOG.TXT line
  {"name", "see", "from": regex, "to": regex}
                                    the time from the LOG line matching "from" to the next
                                    line matching "to", by the lines' time stamps
                                    (tools\\lstamp.asm; the log's own writes left out)
  with "nfrom": n the n-th line matching "from" (default 1; -1 the last), with "nto": n the
  n-th matching "to" after it, with "after": regex the "to" lines are looked for after the
  next line matching it; "agg": "sum" | "median" | "max" | "count" takes every match of a
  "bench" / "log" regex instead of the first ("skip": n leaves out the first n); "per":
  regex divides the value by that regex's first group (a line that times n things).

Per metric and program: the median of the repeats; with several repeats also their
spread ((max - min) / median, in percent); with two programs the second's difference
from the first in percent (negative = faster). Missing values print as "-" and the
exit code is 1.
"""
import json
import os
import re
import statistics
import sys

HZ = 1193182.0
STAMP = re.compile(r'^@([0-9A-F]{8}) ')


def read_log(path):
    """[(counts or None, text)], the stamps made monotonic over the 32-bit wrap."""
    try:
        with open(path, encoding='latin-1', newline='') as f:
            raw = [l.rstrip('\r\n') for l in f]
    except OSError:
        return None
    out = []
    base, last = 0, None
    for l in raw:
        m = STAMP.match(l)
        if m:
            t = int(m.group(1), 16)
            if last is not None and t < last:
                base += 1 << 32
            last = t
            out.append((base + t, l[m.end():]))
        else:
            out.append((None, l))
    return out


def values(lines, rx):
    v = []
    for _, l in lines:
        m = re.search(rx, l)
        if m:
            v.append(float(m.group(1)))
    return v


def metric_value(m, log, bench):
    if 'from' in m:
        if log is None:
            return None
        nf = m.get('nfrom', 1)
        hits = [i for i, (_, l) in enumerate(log) if re.search(m['from'], l)]
        if nf > 0 and len(hits) < nf or nf < 0 and len(hits) < -nf:
            return None
        i = hits[nf - 1] if nf > 0 else hits[nf]
        j = i + 1
        if 'after' in m:
            j = next((k for k in range(i + 1, len(log)) if re.search(m['after'], log[k][1])), len(log))
        ends = [k for k in range(j, len(log)) if re.search(m['to'], log[k][1])]
        nt = m.get('nto', 1)
        if len(ends) < nt:
            return None
        k = ends[nt - 1]
        if log[i][0] is None or log[k][0] is None:
            return None
        return (log[k][0] - log[i][0]) * 1000.0 / HZ
    src = bench if 'bench' in m else log
    if src is None:
        return None
    v = values(src, m.get('bench') or m.get('log'))
    v = v[m.get('skip', 0):]
    if not v:
        return None
    if 'per' in m:
        n = values(src, m['per'])
        return v[0] / n[0] if n and n[0] else None
    agg = m.get('agg', 'first')
    if agg == 'sum':
        return sum(v)
    if agg == 'median':
        return statistics.median(v)
    if agg == 'max':
        return max(v)
    if agg == 'count':
        return float(len(v))
    return v[0]


def fmt(x):
    if x is None:
        return '-'
    return f'{x:.0f}' if x >= 100 else f'{x:.1f}'


def main(argv):
    md = '--md' in argv
    show_runs = '--runs' in argv
    out_json = None
    if '--json' in argv:
        out_json = argv[argv.index('--json') + 1]
        del argv[argv.index('--json'):argv.index('--json') + 2]
    argv = [a for a in argv if a not in ('--md', '--runs')]
    d = argv[0]
    names = argv[argv.index('--') + 1:]
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, 'optcases.json'), encoding='utf-8') as f:
        cases = json.load(f)['cases']
    reps = sorted((x for x in os.listdir(d) if re.fullmatch(r'r\d+', x)), key=lambda x: int(x[1:]))
    rows = []
    bad = 0
    for c in cases:
        if not any(os.path.isdir(os.path.join(d, r, f"{c['name']}-{n}")) for r in reps for n in names):
            continue            # not run this time
        for m in c.get('metrics', []):
            row = {'case': c['name'], 'machine': c['machine'], 'metric': m['name'], 'see': m.get('see', ''),
                   'unit': m.get('unit', 'ms'), 'ms': {}, 'runs': {}, 'spread_pct': {}}
            for n in names:
                vals = []
                for r in reps:
                    p = os.path.join(d, r, f"{c['name']}-{n}")
                    vals.append(metric_value(m, read_log(os.path.join(p, 'LOG.TXT')),
                                             read_log(os.path.join(p, 'BENCH.TXT'))))
                good = [v for v in vals if v is not None]
                row['runs'][n] = vals
                if len(good) < len(vals) or not good:
                    bad += 1
                row['ms'][n] = statistics.median(good) if good else None
                row['spread_pct'][n] = ((max(good) - min(good)) * 100.0 / statistics.median(good)
                                        if len(good) > 1 and statistics.median(good) else None)
            a, b = row['ms'][names[0]], row['ms'][names[-1]]
            row['diff_pct'] = (b - a) * 100.0 / a if len(names) > 1 and a and b is not None else None
            rows.append(row)
    many = len(reps) > 1
    head = ['case', 'metric'] + [f'{n} ms' for n in names]
    if many:
        head += [f'{n} spread %' for n in names]
    if len(names) > 1:
        head.append('diff %')
    if show_runs:
        head += [f'{n} runs' for n in names]
    table = []
    for r in rows:
        line = [r['case'], r['metric'] + ('' if r['unit'] == 'ms' else f" ({r['unit']})")]
        line += [fmt(r['ms'][n]) for n in names]
        if many:
            line += ['-' if r['spread_pct'][n] is None else f"{r['spread_pct'][n]:.1f}" for n in names]
        if len(names) > 1:
            line.append('-' if r['diff_pct'] is None else f"{r['diff_pct']:+.1f}")
        if show_runs:
            line += ['/'.join(fmt(v) for v in r['runs'][n]) for n in names]
        table.append(line)
    if md:
        print('| ' + ' | '.join(head) + ' |')
        print('|' + '---|' * len(head))
        for line in table:
            print('| ' + ' | '.join(line) + ' |')
    else:
        w = [max(len(x[i]) for x in [head] + table) for i in range(len(head))]
        for line in [head] + table:
            print('  '.join(x.ljust(w[i]) if i < 2 else x.rjust(w[i]) for i, x in enumerate(line)).rstrip())
    # Paced waits that ended before their least time (PACE <what> <least ms> <took ms>): on a
    # machine without music the game's clock can step back once in a while (src/timer.c
    # timer_read reads the BIOS tick and the chip's count apart), and a wait under way then ends
    # at once. Where that happens moves with every change of timing, so a span with such a wait
    # in one program and not in the other differs by up to the wait's length (200 ms a hit,
    # 400 ms a blast) for no other reason.
    notes = []
    for c in cases:
        cut = {}
        for n in names:
            for r in reps:
                log = read_log(os.path.join(d, r, f"{c['name']}-{n}", 'LOG.TXT')) or []
                k = [l for _, l in log for m in [re.match(r'PACE \w (\d+) (\d+)$', l)] if m and int(m.group(2)) < int(m.group(1))]
                if k:
                    cut[n] = cut.get(n, []) + k
        if cut:
            notes.append(f"NOTE {c['name']}: paced waits cut short (PACE lines that took less than their least time): "
                         + '; '.join(f"{n}: {', '.join(v)}" for n, v in cut.items()))
    for x in notes:
        print(x)
    if out_json:
        with open(out_json, 'w', encoding='utf-8') as f:
            json.dump({'programs': names, 'repeats': len(reps), 'metrics': rows, 'notes': notes}, f, indent=1)
    if bad:
        print(f'{bad} values missing (a run that did not reach its lines: see run.txt and LOG.TXT in its folder)')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
