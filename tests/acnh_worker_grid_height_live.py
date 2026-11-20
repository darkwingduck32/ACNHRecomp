"""Check captured worker-grid results across stock and handwritten gameplay."""
import argparse
from collections import Counter, defaultdict
from pathlib import Path
import re


WORKER_CALLS = {0x11fca98, 0x11fcac0, 0x11fd270, 0x11fd70c, 0x11fd75c}
TRACE = re.compile(
    r"acnh worker grid height (?P<route>original|cpp) same_input=(?P<comparison>\w+) "
    r"call=(?P<call>[0-9a-f]+) point=(?P<point>[0-9a-f,]+) ground=1 "
    r"before=(?P<material>[57]) override_before=[0-9a-f]+ "
    r"kind=(?P<kind>[0-9a-f]+) override=(?P<override>[0-9a-f]+) out=(?P<height>[0-9a-f]+)"
)


def read(path, route, comparison):
    outcomes = defaultdict(lambda: defaultdict(set))
    counts = Counter()
    for line in path.read_text(errors="replace").splitlines():
        if not line.startswith("acnh worker grid height "):
            continue
        match = TRACE.fullmatch(line)
        if not match or (match["route"], match["comparison"]) != (route, comparison):
            raise ValueError(f"{path}: unexpected worker-grid trace: {line}")
        call = int(match["call"], 16)
        if call not in WORKER_CALLS:
            raise ValueError(f"{path}: unexpected worker-grid caller {call:#x}")
        point = tuple(int(value, 16) for value in match["point"].split(","))
        if len(point) != 3:
            raise ValueError(f"{path}: malformed worker-grid point")
        key = (point, int(match["material"], 16))
        value = tuple(int(match[field], 16) for field in ("kind", "override", "height"))
        outcomes[call][key].add(value)
        counts[call] += 1
    return outcomes, counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("cpp", type=Path)
    parser.add_argument("--sites", nargs="+", type=lambda value: int(value, 16), required=True)
    args = parser.parse_args()
    expected = set(args.sites)
    original, original_counts = read(args.original, "original", "stock")
    cpp, cpp_counts = read(args.cpp, "cpp", "match")
    if cpp_counts.keys() != expected:
        raise SystemExit(f"Expected worker sites {sorted(expected)}, got {sorted(cpp_counts)}")
    for site in sorted(expected):
        shared = original[site].keys() & cpp[site].keys()
        if not shared:
            raise SystemExit(f"{site:#x}: no shared point and material input")
        for key in shared:
            if original[site][key] != cpp[site][key]:
                raise SystemExit(f"{site:#x}: different output on a shared point and material input {key}")
        print(f"{site:#x}: {len(shared)} shared inputs, "
              f"{len(original[site]) - len(shared)} original-only, {len(cpp[site]) - len(shared)} C++-only; "
              f"{original_counts[site]} original / {cpp_counts[site]} C++ samples")
    print(f"All {sum(cpp_counts.values())} C++ samples passed the in-process original on identical input")


if __name__ == "__main__":
    main()
