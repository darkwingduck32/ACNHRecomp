"""Check field action calls in order or compare the two state-dependent leaves."""
import argparse
from collections import Counter, defaultdict
from pathlib import Path
import re


SITES = {
    (0x21e094, 0x226540), (0x21e0f8, 0x221e70), (0x21e17c, 0x221e90), (0x21e208, 0x226ca0),
    (0x21e264, 0x227d30), (0x21e2dc, 0x227cf0), (0x21e3b8, 0x222bb0),
    (0x21e3f4, 0x225d8c), (0x21e3f4, 0x225db8),
}
FOCUSED = {0x21e094, 0x21e17c}
TRACE = re.compile(
    r"acnh field action (original|cpp) entry=([0-9a-f]+) call=([0-9a-f]+)"
    r" event=([0-9a-f]+) writes=([0-9a-f,:]*) same_input=match out=([0-9a-f]+)"
    r"(?: inputs=([0-9a-f:-]+))?"
)


def read(path, route, focus):
    records = []
    counts = Counter()
    with path.open(errors="replace") as log:
        for row in log:
            if not row.startswith("acnh field action "):
                continue
            match = TRACE.fullmatch(row.rstrip())
            if not match or match[1] != route:
                raise ValueError(f"{path}: unexpected field action record: {row.rstrip()}")
            entry, site, event = (int(value, 16) for value in match.groups()[1:4])
            if (entry in FOCUSED) != focus:
                continue
            counts[entry, site] += 1
            records.append((entry, site, event, match[5], int(match[6], 16), match[7]))
    sites = {(entry, site) for entry, site in SITES if (entry in FOCUSED) == focus}
    if counts.keys() != sites:
        raise ValueError(f"{path}: missing sites {sites - counts.keys()} or extra {counts.keys() - sites}")
    print(f"{path}: " + ", ".join(f"{entry:#x}/{site:#x}={count}" for (entry, site), count in sorted(counts.items())))
    return records


def by_input(records):
    states = {}
    for entry, site, event, writes, result, inputs in records:
        if inputs is None:
            raise ValueError(f"missing input state for {entry:#x} at {site:#x}")
        key = entry, site, event, inputs
        if states.setdefault(key, (writes, result)) != (writes, result):
            raise ValueError(f"inconsistent result or writes for {key}")
    return states


def compare_focus(original, cpp):
    before, after = by_input(original), by_input(cpp)
    common = before.keys() & after.keys()
    if {key[0] for key in common} != FOCUSED:
        raise ValueError("both focused bodies need common input states")
    if mismatch := [key for key in common if before[key] != after[key]]:
        raise ValueError(f"different effects for {mismatch[0]}")
    print(f"Matched {len(common)} input states, {len(before) - len(common)} original-only "
          f"and {len(after) - len(common)} C++-only")


def compare_order(original, cpp):
    def sequence(records):
        per_entry = defaultdict(list)
        for entry, *state in records:
            per_entry[entry].append(state)
        return per_entry

    before, after = sequence(original), sequence(cpp)
    for entry in before:
        if before[entry] != after[entry]:
            raise ValueError(f"ordered field action records differ for {entry:#x}")
    print(f"Matched {len(cpp)} gameplay records in per-function order")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("cpp", type=Path)
    parser.add_argument("--focus", action="store_true", help="Compare the two functions by their complete input state")
    args = parser.parse_args()
    original = read(args.original, "original", args.focus)
    cpp = read(args.cpp, "cpp", args.focus)
    (compare_focus if args.focus else compare_order)(original, cpp)


if __name__ == "__main__":
    main()
