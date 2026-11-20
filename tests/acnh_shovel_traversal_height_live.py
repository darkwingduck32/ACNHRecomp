"""Compare ordered direct height and stage-height calls in outer shovel traversal."""
import argparse
from collections import Counter
from pathlib import Path
import re

from tests.acnh_shovel_height_chain_live import ENTRIES as SURFACE_ENTRIES


STAGE_ENTRY = 0x15e06d0
ENTRIES = (*SURFACE_ENTRIES, STAGE_ENTRY)
DIRECT_SITES = {
    0x15e026c: {0x15f11d0, 0x15f1bcc, 0x15f1e3c, 0x15f20d0, 0x15f20e8},
    STAGE_ENTRY: {0x15f1cb8, 0x15f1f28},
}
TRACE = re.compile(
    r"acnh shovel (height chain|traversal height) (original|cpp) entry=([0-9a-f]+)"
    r"(?: same_input=(stock|match|mismatch))? (call=.* out=[0-9a-f]+)"
)


def read_trace(path, route):
    records = []
    direct_sites = {entry: set() for entry in DIRECT_SITES}
    for line in path.read_text(errors="replace").splitlines():
        match = TRACE.fullmatch(line)
        if not match:
            continue
        kind, observed_route, target, same_input, state = match.groups()
        if kind == "height chain" and " scope=12 " not in state:
            continue
        if observed_route != route or same_input == "mismatch":
            raise ValueError(f"{path}: unexpected route or input mismatch: {line}")
        entry = int(target, 16)
        if entry in direct_sites:
            direct_sites[entry].add(int(state.split()[0].removeprefix("call="), 16))
        records.append((entry, state))
    counts = Counter(entry for entry, _ in records)
    if counts.keys() != set(ENTRIES):
        raise ValueError(f"{path}: expected all eight height entries, got {dict(counts)}")
    if direct_sites != DIRECT_SITES:
        raise ValueError(f"{path}: direct call sites differ: {direct_sites}")
    print(f"{path}: " + ", ".join(f"{entry:#x}={counts[entry]}" for entry in ENTRIES))
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("cpp", type=Path)
    args = parser.parse_args()
    original = read_trace(args.original, "original")
    cpp = read_trace(args.cpp, "cpp")
    for index, (reference, actual) in enumerate(zip(original, cpp)):
        if reference != actual:
            raise SystemExit(f"Call {index + 1}: original {reference}, C++ {actual}")
    if len(original) != len(cpp):
        raise SystemExit(f"Call count differs: original {len(original)}, C++ {len(cpp)}")


if __name__ == "__main__":
    main()
