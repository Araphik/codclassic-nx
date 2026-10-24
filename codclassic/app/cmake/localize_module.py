#!/usr/bin/env python3
"""Turns one guest DLL module's static library into a single object whose
internal symbols are local.

On PC each module (*.xdl) is a shared library built with hidden visibility, so
symbols with the same name in different modules (PPCImageConfig,
PPCFuncMappings, __imp__xstart, __savegprlr_14, ...) never meet. Linked
statically into one NRO they would: the linker keeps the first definition and
silently drops the others, so x360shared.xdl ended up reporting the image of
default.xex. Here:

  1. ld -r --whole-archive lib.a  -> one relocatable object
  2. every HIDDEN symbol defined in it becomes LOCAL, except those that live
     in COMDAT groups (inline functions and template instances from the SDK
     headers): the final link deduplicates those groups by name across
     objects, and a local symbol in a discarded group would leave dangling
     references. They are the same code in every module, so sharing them is
     harmless.

The two entry points (ReXModule_Register__<lib>, ReXModule_GetImageInfo__<lib>)
have default visibility and stay global.

usage: localize_module.py <ld> <objcopy> <readelf> <lib.a> <out.o>
"""
import re
import subprocess
import sys


def main():
    ld, objcopy, readelf, lib, out = sys.argv[1:6]
    subprocess.run([ld, "-r", "--whole-archive", lib, "-o", out], check=True)

    groups = subprocess.run([readelf, "-gW", out], check=True, capture_output=True,
                            text=True).stdout
    group_sections = set()
    for line in groups.splitlines():
        m = re.match(r"\s*\[\s*(\d+)\]\s+\S", line)
        if m:
            group_sections.add(int(m.group(1)))

    syms = subprocess.run([readelf, "-sW", out], check=True, capture_output=True,
                          text=True).stdout
    names = set()
    for line in syms.splitlines():
        parts = line.split()
        # Num: Value Size Type Bind Vis Ndx Name
        if len(parts) < 8 or not parts[0].endswith(":"):
            continue
        bind, vis, ndx, name = parts[4], parts[5], parts[6], parts[7]
        if bind not in ("GLOBAL", "WEAK") or vis != "HIDDEN" or not ndx.isdigit():
            continue
        if int(ndx) in group_sections:
            continue
        names.add(name)

    listing = out + ".localize.txt"
    with open(listing, "w") as f:
        f.write("\n".join(sorted(names)) + "\n")
    subprocess.run([objcopy, "--localize-symbols=" + listing, out], check=True)
    print(f"{out}: {len(names)} symbols made local, {len(group_sections)} COMDAT sections kept shared")


if __name__ == "__main__":
    main()
