"""Compare scoped field bookkeeping calls and their final state."""
import argparse
from collections import Counter, defaultdict
from pathlib import Path
import re


SITES = {
    (0x16bfae0, 0x852ae8), (0x16c0910, 0x852af4), (0x194fa00, 0x144c9b8),
    *((0x874bc8, site) for site in (0x87e184, 0x87e1c4, 0x87e204, 0x87e240)),
    (0x128367c, 0x12622a8),
}
TRACE = re.compile(
    r"acnh field bookkeeping (original|cpp) entry=([0-9a-f]+) call=([0-9a-f]+)"
    r" (key=[0-9a-f]+,[0-9a-f]+ writes=[0-9a-f,:]*) same_input=match out=([0-9a-f]+)"
)


def read(path, route):
    calls = defaultdict(list)
    counts = Counter()
    with path.open(errors="replace") as log:
        for row in log:
            if not row.startswith("acnh field bookkeeping "):
                continue
            match = TRACE.fullmatch(row.rstrip())
            if not match or match[1] != route:
                raise ValueError(f"{path}: unexpected record: {row.rstrip()}")
            entry, site = (int(value, 16) for value in match.groups()[1:3])
            counts[entry, site] += 1
            calls[entry].append((site, match[4], int(match[5], 16)))
    if counts.keys() != SITES:
        raise ValueError(f"{path}: missing sites {SITES - counts.keys()} or extra {counts.keys() - SITES}")
    print(f"{path}: " + ", ".join(f"{entry:#x}/{site:#x}={count}" for (entry, site), count in sorted(counts.items())))
    return calls


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("cpp", type=Path)
    args = parser.parse_args()
    original = read(args.original, "original")
    cpp = read(args.cpp, "cpp")
    for entry in original:
        if original[entry] != cpp[entry]:
            raise SystemExit(f"{entry:#x}: ordered field bookkeeping results differ")
    print(f"Matched {sum(map(len, cpp.values()))} records in per-function order")


if __name__ == "__main__":
    main()
