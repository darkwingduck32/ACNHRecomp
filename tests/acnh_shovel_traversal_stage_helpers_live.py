"""Compare ordered stage-name and rule calls from the shovel traversal."""
import argparse
from collections import Counter
from pathlib import Path
import re


NAME_SITES = {0x15f1c0c, 0x15f1e7c, 0x15e8158, 0x15e79f0, 0x15e7ad8, 0x15e0ae4, 0x15e1210, 0x15ef674}
RULE_SITES = {0x15e7bd4, 0x15ef6e8}
SITES = {
    *((0x5cb0a0, site, 0) for site in NAME_SITES),
    *((0x5bd550, site + 0xc, 0) for site in NAME_SITES),
    *((0x16baf44, site, site) for site in RULE_SITES),
    *((0x16f37d0, 0x16baf54, site) for site in RULE_SITES),
    *((0x16f3a18, 0x16bb50c, site) for site in RULE_SITES),
}
TRACE = re.compile(
    r"acnh shovel traversal stage helper (original|cpp) entry=([0-9a-f]+)"
    r" (call=([0-9a-f]+)(?: from=([0-9a-f]+))? .* out=[0-9a-f]+)"
)


def read(path, route):
    records = []
    sites = Counter()
    with path.open(errors="replace") as log:
        for row in log:
            if not row.startswith("acnh shovel traversal stage helper "):
                continue
            match = TRACE.fullmatch(row.rstrip())
            if not match or match[1] != route:
                raise ValueError(f"{path}: malformed stage helper row: {row.rstrip()}")
            entry, site = int(match[2], 16), int(match[4], 16)
            sites[entry, site, int(match[5] or "0", 16)] += 1
            records.append((entry, match[3]))
    if sites.keys() != SITES:
        raise ValueError(f"{path}: missing call sites {SITES - sites.keys()}, extra {sites.keys() - SITES}")
    counts = Counter(entry for entry, _ in records)
    print(f"{path}: " + ", ".join(f"{entry:#x}={count}" for entry, count in sorted(counts.items())))
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
                raise SystemExit(f"Call {index}: original {before}; C++ {after}")
        raise SystemExit(f"Different stage helper call counts ({len(original)} original, {len(cpp)} C++)")
    print(f"Matched {len(cpp)} ordered traversal stage helper calls")


if __name__ == "__main__":
    main()
