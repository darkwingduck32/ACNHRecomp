"""Check coverage and same-input original comparisons for a clearance gameplay group."""
import argparse
from collections import Counter
from pathlib import Path
import re


SITES = {
    0x1161d64: {(1, 0, 9), (1, 1, 1)},
    0x1161d78: {(0, 0, 9), (0, 1, 1)},
    0x15f4adc: {(1, 0, 9)},
    0x15f4af0: {(0, 0, 9)},
    0x15f59dc: {(0, 0, 9), (1, 0, 9)},
}
RECORD = re.compile(
    r"acnh terrain clearance (original|cpp) call=([a-f0-9]+) selector=([01]) "
    r"radius=([a-f0-9]+) w0=([01]) s0=([a-f0-9]+) queries=(\d+) summary=([a-f0-9]+) "
    r"same_input=(match|mismatch)"
)


def survey(path, route):
    counts, cases = Counter(), set()
    with path.open(encoding="utf-8", errors="replace") as lines:
        for line in lines:
            if "acnh terrain clearance " not in line:
                continue
            record = RECORD.fullmatch(line.strip())
            if not record:
                raise ValueError(f"Malformed clearance log in {path}: {line.strip()}")
            observed, caller, selector, radius, hit, scalar, count, summary, status = record.groups()
            site = int(caller, 16)
            if observed != route or site not in SITES or status != "match":
                raise ValueError(f"Unexpected clearance result in {path}: {line.strip()}")
            width = int(count)
            if not 0 <= width <= 9:
                raise ValueError(f"Invalid query count in {path}: {line.strip()}")
            counts[site] += 1
            cases.add((site, int(selector), int(hit), width, int(radius, 16), int(summary, 16)))
    for site, expected in SITES.items():
        observed = {(selector, hit, width) for caller, selector, hit, width, _, _ in cases if caller == site}
        if missing := expected - observed:
            raise ValueError(f"Missing {site:#x} coverage in {path}: {sorted(missing)}")
    print(f"{path.parent.name}: {sum(counts.values())} {route} records, five sites, all same-input checks match")
    return counts, cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("cpp", type=Path, nargs="*")
    args = parser.parse_args()
    reference, original = survey(args.original, "original")
    for path in args.cpp:
        counts, cases = survey(path, "cpp")
        print(f"Sampling differences: original-only classes {len(original-cases)}, "
              f"C++-only classes {len(cases-original)}, "
              f"per-site count deltas {dict((f'{site:x}', counts[site]-reference[site]) for site in SITES)}")


if __name__ == "__main__":
    main()
