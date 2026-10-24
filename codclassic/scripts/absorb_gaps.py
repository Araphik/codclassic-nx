#!/usr/bin/env python3
"""Third pass over uncovered code: hints that prune_gaps_strict.py dropped only because the new
function absorbs an original "function" that was really its own tail.

The analyzer often splits off the target of a forward branch (e.g. the second return path of a
qsort comparator) as a separate sub_X; the real start before it stays uncovered, and an indirect
call there dies with "Call to invalid or unregistered function" (game_sp 0x8707D410 called by
qsort from 0x87096480 when the player dies). A hint on the real start makes the codegen merge the
tail back. That is accepted when every original body either stays identical or disappears into a
new function that starts before it and is its only neighbour, and no surviving code references a
disappeared name.

usage: absorb_gaps.py <rexglue> <app dir> <min bytes>
"""
import glob
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
from gaps import gaps  # noqa: E402
from prune_gaps_strict import MODULES, bodies, read_hints, write_hints  # noqa: E402


def codegen(rexglue, app):
    subprocess.run([rexglue, 'codegen', '--ignore-stamp', 'codclassic_manifest.toml'], cwd=app, check=True,
                   capture_output=True)


def norm(lines):
    return [l.replace('__imp__sub_', 'sub_') for l in lines]


def special_addrs(app, m):
    init = open(os.path.join(app, 'generated', m, 'codclassic_init.cpp'), encoding='utf-8').read()
    return sorted(int(a, 16) for a, name in re.findall(r'\{ 0x([0-9A-F]{8}), (\w+) \}', init)
                  if not name.startswith('sub_'))


def main():
    rexglue, app, minimum = sys.argv[1], sys.argv[2], int(sys.argv[3])
    only = {int(x, 16) for x in sys.argv[4].split(',')} if len(sys.argv) > 4 else None
    hooked = {int(l.strip()[4:], 16) for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'hooked_functions.txt')) if l.startswith('sub_')}
    codegen(rexglue, app)
    base = {m: bodies(os.path.join(app, 'generated', m)) for m in MODULES}
    hints = {m: read_hints(app, m) for m in MODULES}
    cand = {}
    for m in MODULES:
        sp = special_addrs(app, m)
        c = set()
        for start, end in gaps(os.path.join(app, 'generated', m))[0]:
            if end - start < minimum:
                continue
            # Save/restore helpers and import thunk tables: leave alone.
            if any(start - 0x200 <= a < end + 0x200 for a in sp) or any(start <= a < end for a in hooked):
                continue
            if only is None or start in only:
                c.add(start)
        cand[m] = c
        print(f'{m}: {len(c)} candidates')
    for rnd in range(30):
        for m in MODULES:
            write_hints(app, m, hints[m] | cand[m])
        codegen(rexglue, app)
        dropped = 0
        bad = 0
        for m in MODULES:
            now = bodies(os.path.join(app, 'generated', m))
            text_now = {k: ''.join(v) for k, v in now.items()}
            gone = set()
            for name, body in base[m].items():
                if name not in now:
                    gone.add(name)
                    continue
                if norm(now[name]) == norm(body):
                    continue
                bad += 1
                old = ''.join(body)
                for a in re.findall(r'sub_([0-9A-F]{8})', text_now[name]):
                    if f'sub_{a}' not in old and int(a, 16) in cand[m]:
                        cand[m].discard(int(a, 16))
                        dropped += 1
            starts = sorted(set(int(k[4:], 16) for k in now if k.startswith('sub_')))
            for name in gone:
                addr = int(name[4:], 16)
                owner = max((s for s in starts if s < addr), default=None)
                referenced = any(re.search(rf'\b{name}\b', t) for t in text_now.values())
                if owner is None or owner not in cand[m] or referenced:
                    bad += 1
                    if owner in cand[m]:
                        cand[m].discard(owner)
                        dropped += 1
                else:
                    print(f'  {m}: {name} absorbed by sub_{owner:08X}')
        print(f'round {rnd}: {bad} problems, dropped {dropped}, kept {sum(map(len, cand.values()))}')
        if bad == 0:
            for m in MODULES:
                for a in sorted(cand[m]):
                    print(f'  {m}: new 0x{a:08X}')
            return
        if dropped == 0:
            sys.exit('problems but no candidate to blame')
    sys.exit('did not converge')


if __name__ == '__main__':
    main()
