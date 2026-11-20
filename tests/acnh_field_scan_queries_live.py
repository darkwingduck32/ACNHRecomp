"""Compare six field-scan bodies and their observed call order in gameplay."""
import argparse
from collections import Counter
from pathlib import Path
import re


SITES = {
    0x19d6a30: {0x157a698, 0x157abd0},
    0x127d59c: {0x157c628},
    0x1596154: {0x157e75c, 0x157e924},
    0x19d9fd0: {0x157ac20, 0x157a4d0},
    0x19f49bc: {0x157a674},
    0x1217cc0: {0x157c638},
}
TRACE = re.compile(
    r"acnh field scan (original|cpp) entry=([0-9a-f]+) call=([0-9a-f]+)"
    r" (selector=[0-9a-f]+ position=[0-9a-f]+,[0-9a-f]+ writes=[0-9a-f,:]*)"
    r" same_input=match out=([0-9a-f]+)"
)


def read(path, route):
    records = []
    counts = Counter()
    with path.open(errors="replace") as log:
        for row in log:
            if not row.startswith("acnh field scan "):
                continue
            match = TRACE.fullmatch(row.rstrip())
            if not match or match[1] != route:
                raise ValueError(f"{path}: unexpected field scan record: {row.rstrip()}")
            entry, site = (int(value, 16) for value in match.groups()[1:3])
            counts[entry, site] += 1
            records.append((entry, site, match[4], int(match[5], 16)))
    expected = {(entry, site) for entry, sites in SITES.items() for site in sites}
    if counts.keys() != expected:
        raise ValueError(f"{path}: missing sites {expected - counts.keys()} or extra sites {counts.keys() - expected}")
    print(f"{path}: " + ", ".join(f"{entry:#x}/{site:#x}={count}" for (entry, site), count in sorted(counts.items())))
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("cpp", type=Path)
    args = parser.parse_args()
    original = read(args.original, "original")
    cpp = read(args.cpp, "cpp")
    if original != cpp:
        for index, (before, after) in enumerate(zip(original, cpp), 1):
            if before != after:
                raise SystemExit(f"Record {index}: original {before}; C++ {after}")
        raise SystemExit(f"Different field scan counts ({len(original)} original, {len(cpp)} C++)")
    print(f"Matched {len(cpp)} ordered field scan records")


if __name__ == "__main__":
    main()
