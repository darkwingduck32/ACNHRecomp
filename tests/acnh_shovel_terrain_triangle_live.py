"""Check live terrain triangle parity on the original and C++ routes."""
import argparse
from collections import Counter
from pathlib import Path
import re

from tests.acnh_shovel_terrain_triangle_replay import ENTRIES


TRACE = re.compile(r"acnh terrain triangle (original|cpp) entry=([0-9a-f]+) call=([0-9a-f]+)"
                   r" tile=(-?\d+),(-?\d+) same_input=(match|mismatch) out=([01])")


def read(path, route):
    calls = Counter()
    classes = set()
    with path.open(errors="replace") as log:
        for row in log:
            if not row.startswith("acnh terrain triangle "):
                continue
            match = TRACE.fullmatch(row.rstrip("\n"))
            if not match or match[1] != route or match[6] != "match":
                raise ValueError(f"{path}: {row.rstrip()}")
            entry, caller = int(match[2], 16), int(match[3], 16)
            calls[entry] += 1
            classes.add((entry, caller, int(match[4]), int(match[5]), int(match[7])))
    missing = {entry for entry, _ in ENTRIES} - calls.keys()
    if missing:
        raise ValueError(f"{path}: missing {[hex(entry) for entry in sorted(missing)]}")
    print(f"{path}: " + ", ".join(f"{entry:#x}={calls[entry]}" for entry, _ in ENTRIES))
    return classes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("cpp", type=Path)
    args = parser.parse_args()
    original = read(args.original, "original")
    cpp = read(args.cpp, "cpp")
    original_sites = {(entry, caller) for entry, caller, *_ in original}
    cpp_sites = {(entry, caller) for entry, caller, *_ in cpp}
    if original_sites != cpp_sites:
        raise SystemExit(f"Caller coverage differs: {original_sites - cpp_sites} versus {cpp_sites - original_sites}")
    if unmatched := cpp - original:
        raise SystemExit(f"{len(unmatched)} C++ inputs or results absent from original: {sorted(unmatched)[:10]}")
    print(f"{len(cpp)} sampled combinations matched across {len(cpp_sites)} caller sites")
    print(f"{len(original - cpp)} original combinations outside the bounded sample")


if __name__ == "__main__":
    main()
