"""Compare the stage subtree reached from shovel traversal and sweep probes."""
import argparse
from collections import Counter
from pathlib import Path
import re


ENTRIES = (0x15e06d0, 0x15e80f0, 0x83a950, 0x862700, 0x15e7980, 0x195190c, 0x194f798, 0x11eb21c)
TRACE = re.compile(
    r"acnh shovel (height chain|traversal stage) (original|cpp) entry=([0-9a-f]+)"
    r"(?: same_input=(stock|match|mismatch))? (call=.* out=[0-9a-f]+)"
)


def read_trace(path, route):
    cases = []
    for line in path.read_text(errors="replace").splitlines():
        match = TRACE.fullmatch(line)
        if not match:
            continue
        kind, observed, target, same_input, state = match.groups()
        if kind == "height chain" and " scope=14 " not in state:
            continue
        if observed != route or same_input == "mismatch":
            raise ValueError(f"{path}: unexpected route or input mismatch: {line}")
        cases.append((int(target, 16), state))
    counts = Counter(target for target, _ in cases)
    if counts.keys() != set(ENTRIES):
        raise ValueError(f"{path}: expected eight stage bodies, got {dict(counts)}")
    print(f"{path}: " + ", ".join(f"{entry:#x}={counts[entry]}" for entry in ENTRIES))
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("cpp", type=Path)
    args = parser.parse_args()
    original = read_trace(args.original, "original")
    cpp = read_trace(args.cpp, "cpp")
    for position, (expected, actual) in enumerate(zip(original, cpp)):
        if expected != actual:
            raise SystemExit(f"Call {position + 1}: original {expected}, C++ {actual}")
    if len(original) != len(cpp):
        raise SystemExit(f"Call count differs: original {len(original)}, C++ {len(cpp)}")


if __name__ == "__main__":
    main()
