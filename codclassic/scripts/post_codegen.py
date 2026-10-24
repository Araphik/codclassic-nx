#!/usr/bin/env python3
"""Run after every `rexglue codegen` of codclassic/app.

1. Checks that every function hooked in app/src (REX_HOOK_RAW(sub_X) and friends) is listed in
   scripts/hooked_functions.txt, and adds the missing ones.
2. Runs direct_calls.py on every generated module: calls to functions with no hook become direct
   calls to __imp__sub_X (inlinable); calls to hooked functions keep going through sub_X, so the hook
   is used. Forgetting this step makes the build silently ignore new hooks, or lose speed.

usage: post_codegen.py            (from anywhere)
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
APP = os.path.join(ROOT, 'app')
HOOKED = os.path.join(HERE, 'hooked_functions.txt')
MODULES = ['default', 'x360shared', 'game_sp', 'cgame_sp', 'ui_sp']


def main():
    listed = set(open(HOOKED, encoding='utf-8').read().split()) if os.path.exists(HOOKED) else set()
    found = set()
    src = os.path.join(APP, 'src')
    for name in os.listdir(src):
        text = open(os.path.join(src, name), encoding='utf-8', errors='ignore').read()
        found |= set(re.findall(r'REX_(?:HOOK|STUB)[A-Z_]*\((sub_[0-9A-F]{8})', text))
    missing = sorted(found - listed)
    if missing:
        print('adding to hooked_functions.txt:', ' '.join(missing))
        listed |= found
        open(HOOKED, 'w', encoding='utf-8').write('\n'.join(sorted(listed)) + '\n')
    for m in MODULES:
        gen = os.path.join(APP, 'generated', m)
        if not os.path.isdir(gen):
            print(f'skipping {m}: {gen} does not exist (run rexglue codegen first)')
            continue
        for extra in (['--deshacer'], []):
            subprocess.run([sys.executable, os.path.join(HERE, 'direct_calls.py'), *extra, '--gen', gen,
                            '--enganchadas', HOOKED], check=True)


if __name__ == '__main__':
    main()
