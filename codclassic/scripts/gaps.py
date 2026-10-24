#!/usr/bin/env python3
"""Finds code that no recompiled function covers, per module.

Every PPC instruction is 4 bytes and the codegen writes one "\t// <asm>" comment line per
instruction, so a function's end is start + 4 * comments. Stretches between one function's end
and the next start are code nobody owns: an indirect call (vtable, callback table) into one of
them dies with "Call to invalid or unregistered function".

  gaps.py list  <generated/mod>                 -> print gaps
  gaps.py split <generated/mod> <gap-hints>     -> after a codegen pass where every gap was
                                                   declared with an explicit end, cut each gap into
                                                   functions at blr / unconditional b and print the
                                                   final hints
"""
import os
import re
import sys

FUNC = re.compile(r'^(?:DEFINE_REX_FUNC|PPC_FUNC_IMPL)\((?:__imp__)?sub_([0-9A-F]{8})\)')


def functions(gen):
    """{start: [asm lines]} from every recomp file of a module."""
    out = {}
    for f in sorted(os.listdir(gen)):
        if not (f.startswith('codclassic_recomp.') and f.endswith('.cpp')):
            continue
        cur = None
        for line in open(os.path.join(gen, f), encoding='utf-8'):
            m = FUNC.match(line)
            if m:
                cur = int(m.group(1), 16)
                out[cur] = []
                continue
            if cur is not None:
                if line.startswith('}'):
                    cur = None
                elif line.startswith('\t// '):
                    out[cur].append(line[4:].strip())
    return out


def code_range(gen):
    txt = open(os.path.join(gen, 'codclassic_pch.h'), encoding='utf-8').read()
    base = int(re.search(r'#define REX_CODE_BASE (0x[0-9A-Fa-f]+)', txt).group(1), 16)
    size = int(re.search(r'#define REX_CODE_SIZE (0x[0-9A-Fa-f]+)', txt).group(1), 16)
    return base, base + size


def gaps(gen, minimum=8):
    fns = functions(gen)
    lo, hi = code_range(gen)
    starts = sorted(fns)
    res = []
    prev_end = lo
    for s in starts + [hi]:
        if s - prev_end >= minimum and s <= hi:
            res.append((prev_end, s))
        if s in fns:
            prev_end = max(prev_end, s + 4 * len(fns[s]))
    return res, fns


ENDS = re.compile(r'^(blr|bctr|b\s+0x[0-9a-fA-F]+)\s*$')


def split(gen, gap_list):
    """Cut declared gap functions into real functions."""
    fns = functions(gen)
    hints = []
    for lo, hi in gap_list:
        asm = fns.get(lo)
        if asm is None:
            continue
        start_new = True
        for i, ins in enumerate(asm):
            addr = lo + 4 * i
            if addr >= hi:
                break
            padding = ins in ('nop', '.long 0x0', 'twi 0,r0,0') or ins.startswith('.long 0x0')
            if start_new and not padding:
                hints.append(addr)
                start_new = False
            if ENDS.match(ins):
                start_new = True
    return hints


def main():
    cmd, gen = sys.argv[1], sys.argv[2]
    if cmd == 'list':
        g, _ = gaps(gen)
        for lo, hi in g:
            print(f'0x{lo:08X} 0x{hi:08X} {hi - lo}')
    elif cmd == 'split':
        g = [tuple(int(x, 16) for x in l.split()[:2]) for l in open(sys.argv[3]) if l.strip()]
        for a in split(gen, g):
            print(f'0x{a:08X}')


if __name__ == '__main__':
    main()
