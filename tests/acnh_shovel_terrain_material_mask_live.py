"""Compare ground and overlay terrain material queries in gameplay."""
import argparse
from collections import Counter
from pathlib import Path
import re


SITES = {0x84b728: 0x15753e0, 0x84b840: 0x15755c0}
TRACE = re.compile(
    r"acnh terrain material (original|cpp) entry=([0-9a-f]+) call=([0-9a-f]+)"
    r" tile=(-?\d+),(-?\d+) mask=([0-9a-f]{8},[0-9a-f]{8},[0-9a-f]{8})"
    r" same_input=(match|mismatch) out=([01])"
)


def observed(path, route):
    calls = []
    counts = Counter()
    with path.open(errors="replace") as log:
        for line in log:
            if not line.startswith("acnh terrain material "):
                continue
            result = TRACE.fullmatch(line.rstrip())
            if not result or result[1] != route or result[7] != "match":
                raise ValueError(f"{path}: {line.rstrip()}")
            entry, site = (int(value, 16) for value in result.groups()[1:3])
            if SITES.get(entry) != site:
                raise ValueError(f"{path}: unexpected caller {site:#x} of {entry:#x}")
            calls.append((entry, int(result[4]), int(result[5]), result[6], int(result[8])))
            counts[entry, int(result[8])] += 1
    for entry in SITES:
        if not (counts[entry, 0] or counts[entry, 1]):
            raise ValueError(f"{path}: entry {entry:#x} was missed")
        print(f"{entry:#x}: {counts[entry, 0]} false and {counts[entry, 1]} true calls")
    return calls


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("cpp", type=Path)
    args = parser.parse_args()
    original = observed(args.original, "original")
    cpp = observed(args.cpp, "cpp")
    if original != cpp:
        raise SystemExit(f"Ordered material calls differ ({len(original)} original, {len(cpp)} C++)")
    print(f"Matched {len(cpp)} ordered material queries")


if __name__ == "__main__":
    main()
