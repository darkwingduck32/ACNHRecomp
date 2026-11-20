"""Compare the two observed traversal legs and their height queries."""
import argparse
from collections import Counter
from pathlib import Path
import re

from tests.acnh_shovel_height_chain_live import ENTRIES as SURFACE_ENTRIES


ENTRIES = {0x15f10f0, 0x15f27e0, *SURFACE_ENTRIES}
TRACE = re.compile(
    r"acnh shovel (traversal leg peak|traversal leg|height chain) (original|cpp)"
    r" entry=([0-9a-f]+)(?: same_input=(stock|match|mismatch))? (call=.* out=[0-9a-f,]+)"
)


def read(path, route):
    calls = []
    counts = Counter()
    sites = Counter()
    with path.open(errors="replace") as log:
        for row in log:
            match = TRACE.fullmatch(row.rstrip())
            if not match:
                continue
            kind, mode, raw_entry, same_input, state = match.groups()
            if kind == "height chain" and " scope=15 " not in state:
                continue
            if mode != route or same_input == "mismatch":
                raise ValueError(f"{path}: unexpected route or parity failure: {row.rstrip()}")
            entry = int(raw_entry, 16)
            site = int(state.split(maxsplit=1)[0].removeprefix("call="), 16)
            counts[entry] += 1
            sites[entry, site] += 1
            calls.append((entry, state))
    if any((0x15f10f0, site) not in sites for site in (0x15f1dd8, 0x15f1e24)):
        raise ValueError(f"{path}: both recursive caller sites must be present")
    if counts.keys() != ENTRIES:
        raise ValueError(f"{path}: expected traversal, peak, and seven height entries, got {dict(counts)}")
    print(f"{path}: " + ", ".join(f"{entry:#x}={count}" for entry, count in sorted(counts.items())))
    return calls


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("cpp", type=Path)
    args = parser.parse_args()
    original = read(args.original, "original")
    cpp = read(args.cpp, "cpp")
    if original != cpp:
        for index, (expected, actual) in enumerate(zip(original, cpp), 1):
            if expected != actual:
                raise SystemExit(f"Call {index}: original {expected}; C++ {actual}")
        raise SystemExit(f"Different call totals, {len(original)} original and {len(cpp)} C++")
    print(f"Matched {len(cpp)} ordered recursive traversal calls")


if __name__ == "__main__":
    main()
