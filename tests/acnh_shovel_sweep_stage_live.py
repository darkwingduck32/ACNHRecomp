"""Compare ordered stage helper or destination record calls inside shovel sweeps."""
import argparse
from collections import Counter
from pathlib import Path
import re


ENTRIES = (0x5cb0a0, 0x5bd550, 0x16f37d0, 0x16f3a18, 0x16baf44)
RECORD_ENTRIES = (0x194f798, 0x11eb21c, 0x195190c)


def read_trace(path, route, records_only=False):
    entries = RECORD_ENTRIES if records_only else ENTRIES
    prefix = "acnh shovel sweep record " if records_only else "acnh shovel sweep stage "
    trace = re.compile(re.escape(prefix) + r"(original|cpp) entry=([0-9a-f]+) (call=.* out=[0-9a-f]+)")
    records = []
    for line in path.read_text(errors="replace").splitlines():
        if not line.startswith(prefix):
            continue
        match = trace.fullmatch(line)
        if not match or match[1] != route:
            raise ValueError(f"{path}: wrong stage route or trace format: {line}")
        records.append((int(match[2], 16), match[3]))
    counts = Counter(entry for entry, _ in records)
    if counts.keys() != set(entries):
        raise ValueError(f"{path}: expected {len(entries)} different entries, got {dict(counts)}")
    print(f"{path}: " + ", ".join(f"{entry:#x}={counts[entry]}" for entry in entries))
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("cpp", type=Path)
    parser.add_argument("--record", action="store_true", help="Compare the destination record, bounds, and provider")
    args = parser.parse_args()
    if read_trace(args.original, "original", args.record) != read_trace(args.cpp, "cpp", args.record):
        raise SystemExit("Ordered stage helper inputs or outputs differ")


if __name__ == "__main__":
    main()
