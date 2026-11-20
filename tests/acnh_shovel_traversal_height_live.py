"""Compare ordered height calls under the shovel traversal parent or sweep probe."""
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
SWEEP_PROBE = 0x15e11a0
SWEEP_SITES = {SWEEP_PROBE: {0x15ede4c, 0x15ef5ac}, 0x15e026c: {0x15e11d0}}
TRACE = re.compile(
    r"acnh shovel (height chain|traversal height|sweep probe) (original|cpp) entry=([0-9a-f]+)"
    r"(?: same_input=(stock|match|mismatch))? (call=.* out=[0-9a-f]+)"
)


def read_trace(path, route, sweep=False):
    direct = SWEEP_SITES if sweep else DIRECT_SITES
    entries = (*SURFACE_ENTRIES, SWEEP_PROBE) if sweep else ENTRIES
    trace_kind = "sweep probe" if sweep else "traversal height"
    scope = 13 if sweep else 12
    records = []
    direct_sites = {entry: set() for entry in direct}
    for line in path.read_text(errors="replace").splitlines():
        match = TRACE.fullmatch(line)
        if not match:
            continue
        kind, observed_route, target, same_input, state = match.groups()
        if kind == "height chain":
            if f" scope={scope} " not in state:
                continue
        elif kind != trace_kind:
            continue
        if observed_route != route or same_input == "mismatch":
            raise ValueError(f"{path}: unexpected route or input mismatch: {line}")
        entry = int(target, 16)
        if entry in direct_sites:
            direct_sites[entry].add(int(state.split()[0].removeprefix("call="), 16))
        records.append((entry, state))
    counts = Counter(entry for entry, _ in records)
    if counts.keys() != set(entries):
        raise ValueError(f"{path}: expected all eight height entries, got {dict(counts)}")
    if direct_sites != direct:
        raise ValueError(f"{path}: direct call sites differ: {direct_sites}")
    print(f"{path}: " + ", ".join(f"{entry:#x}={counts[entry]}" for entry in entries))
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("cpp", type=Path)
    parser.add_argument("--sweep-probe", action="store_true")
    args = parser.parse_args()
    original = read_trace(args.original, "original", args.sweep_probe)
    cpp = read_trace(args.cpp, "cpp", args.sweep_probe)
    for index, (reference, actual) in enumerate(zip(original, cpp)):
        if reference != actual:
            raise SystemExit(f"Call {index + 1}: original {reference}, C++ {actual}")
    if len(original) != len(cpp):
        raise SystemExit(f"Call count differs: original {len(original)}, C++ {len(cpp)}")


if __name__ == "__main__":
    main()
