"""Compare ordered stage-name, hash and filter calls inside shovel sweeps."""
import argparse
from collections import Counter
from pathlib import Path
import re


TRACE = re.compile(r"acnh shovel sweep stage (original|cpp) entry=([0-9a-f]+) (call=.* out=[0-9a-f]+)")
ENTRIES = (0x5cb0a0, 0x5bd550, 0x16f37d0, 0x16f3a18, 0x16baf44)


def read_trace(path, route):
    records = []
    for line in path.read_text(errors="replace").splitlines():
        if not line.startswith("acnh shovel sweep stage "):
            continue
        match = TRACE.fullmatch(line)
        if not match or match[1] != route:
            raise ValueError(f"{path}: wrong stage route or trace format: {line}")
        records.append((int(match[2], 16), match[3]))
    counts = Counter(entry for entry, _ in records)
    if counts.keys() != set(ENTRIES):
        raise ValueError(f"{path}: expected all five stage helpers, got {dict(counts)}")
    print(f"{path}: " + ", ".join(f"{entry:#x}={counts[entry]}" for entry in ENTRIES))
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("cpp", type=Path)
    args = parser.parse_args()
    if read_trace(args.original, "original") != read_trace(args.cpp, "cpp"):
        raise SystemExit("Ordered stage helper inputs or outputs differ")


if __name__ == "__main__":
    main()
