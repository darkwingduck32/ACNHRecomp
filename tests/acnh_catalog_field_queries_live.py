"""Compare the observed catalog queries and description output in gameplay."""
import argparse
from collections import Counter, defaultdict
from pathlib import Path
import re


DESCRIPTION = 0x19d7ca0
SITES = {
    0x19d778c: {0x24572c0}, 0x19d78cc: {0x2225f58, 0x2549830},
    0x19e89e0: {0x659b6c, 0x6661b8, 0x163a3b0},
    0x19e8b58: {0x1289668, 0x142de9c, 0x143133c},
    DESCRIPTION: {0x1602930, 0x160fb50, 0x160fd50, 0x1610db4},
}
TRACE = re.compile(
    r"acnh catalog field (original|cpp) entry=([0-9a-f]+) call=([0-9a-f]+)"
    r" (key=[0-9a-f]+ writes=[0-9a-f,:]*) same_input=(match|mismatch) out=([0-9a-f]+)"
)


def read(path, route, entries=SITES):
    calls = defaultdict(list)
    counts = Counter()
    with path.open(errors="replace") as log:
        for row in log:
            if not row.startswith("acnh catalog field "):
                continue
            match = TRACE.fullmatch(row.rstrip())
            if not match:
                raise ValueError(f"{path}: unexpected record: {row.rstrip()}")
            entry, site = (int(value, 16) for value in match.groups()[1:3])
            if entry not in entries and entry in SITES:
                continue
            if match[1] != route or match[5] != "match":
                raise ValueError(f"{path}: unexpected record: {row.rstrip()}")
            counts[entry, site] += 1
            calls[entry, site].append((match[4], int(match[6], 16)))
    expected = {(entry, site) for entry in entries for site in SITES[entry]}
    if counts.keys() != expected:
        raise ValueError(f"{path}: missing sites {expected - counts.keys()} or extra {counts.keys() - expected}")
    print(f"{path}: " + ", ".join(f"{entry:#x}/{site:#x}={count}" for (entry, site), count in sorted(counts.items())))
    return calls


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("cpp", type=Path)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--queries", action="store_true",
                       help="Compare read-only result classes after per-call same-input checks.")
    group.add_argument("--description", action="store_true",
                       help="Compare only the ordered catalog description outputs.")
    args = parser.parse_args()
    entries = SITES.keys() - {DESCRIPTION} if args.queries else {DESCRIPTION} if args.description else SITES
    original = read(args.original, "original", entries)
    cpp = read(args.cpp, "cpp", entries)
    for entry, site in original:
        reference, candidate = original[entry, site], cpp[entry, site]
        if args.queries:
            reference, candidate = set(reference), set(candidate)
        if reference != candidate:
            raise SystemExit(f"{entry:#x} at {site:#x}: catalog results or effects differ")
    if args.queries:
        classes = sum(len(set(calls)) for calls in cpp.values())
        print(f"Matched {classes} per-site result classes with {sum(map(len, cpp.values()))} C++ same-input checks")
    else:
        print(f"Matched {sum(map(len, cpp.values()))} catalog records in per-site order")


if __name__ == "__main__":
    main()
