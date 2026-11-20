"""Compare ordered row lookups and shared cache transitions in gameplay."""
import argparse
from collections import Counter
from pathlib import Path
import re
from typing import NamedTuple

ENTRIES = (0x29cc340, 0x29cc470, 0x29cc5a0, 0x29cc6d0,
           0x29cc800, 0x29cc930, 0x29cca60, 0x29ccb90)
SITES = (0x29cd410, 0x29cd420, 0x29cd430, 0x29cd440,
         0x29cd450, 0x29cd460, 0x29cd470, 0x29cd490)
EARLY_ENTRIES = (0x29cbe80, 0x29cbfb0, 0x29cc0e0, 0x29cc210)
EARLY_SITES = (0x29cd3d0, 0x29cd3e0, 0x29cd3f0, 0x29cd400)
SLOTS = dict(zip(ENTRIES + EARLY_ENTRIES, (16, 1, 2, 3, 4, 5, 6, 7, 12, 13, 14, 15), strict=True))
TRACE_FIELDS = (
    r"(original|cpp) entry=([0-9a-f]+) call=([0-9a-f]+)"
    r" parent=([0-9a-f]+) owner=([0-9a-f]+) request=([0-9a-f]+) back=([0-9a-f]+)"
    r" row=([01]) input=([0-9a-f]+)"
    r" before=([0-9a-f]{290}) after=([0-9a-f]{290}) out=([0-9a-f]+)"
)


class ReaderCall(NamedTuple):
    entry: int
    request: int
    back: int
    row_present: int
    input_word: int
    before: bytes
    after: bytes
    result: int

    def warm_slot(self):
        offset = 1 + 4 * SLOTS[self.entry]
        return int.from_bytes(self.before[offset:offset + 4], "little", signed=True)

    def resolves_slot(self):
        return self.row_present and self.back and (not self.before[0] or self.warm_slot() == -1)


def read_groups(path, expected_route, *, early=False):
    entries, sites = (EARLY_ENTRIES, EARLY_SITES) if early else (ENTRIES, SITES)
    prefix = "acnh field value early " if early else "acnh field value "
    trace = re.compile(prefix + TRACE_FIELDS)
    groups = []
    calls = []
    count = Counter()
    identity = None
    for line in path.read_text(errors="replace").splitlines():
        if not line.startswith(prefix) or (not early and line.startswith("acnh field value early ")):
            continue
        match = trace.fullmatch(line)
        if not match or match[1] != expected_route:
            raise ValueError(f"{path}: unexpected trace: {line}")
        entry, site, frame, owner, requested, back = (int(value, 16) for value in match.groups()[1:7])
        present, word, before, after, out = match.groups()[7:]
        call = ReaderCall(entry, requested, back, int(present), int(word, 16),
                          bytes.fromhex(before), bytes.fromhex(after), int(out, 16))
        if entry == entries[0]:
            if calls:
                if len(calls) < len(entries) and calls[-1].result != calls[-1].request:
                    raise ValueError(f"{path}: incomplete parent sequence")
                groups.append(tuple(calls))
            calls = []
            identity = frame, owner, requested, back
        index = len(calls)
        if index >= len(entries) or entry != entries[index] or site != sites[index]:
            raise ValueError(f"{path}: unexpected order at {entry:#x} from {site:#x}")
        if identity != (frame, owner, requested, back):
            raise ValueError(f"{path}: parent frame, owner, or request changed")
        if calls and (calls[-1].result == requested or calls[-1].after != call.before):
            raise ValueError(f"{path}: unexpected comparison or cache transition")
        calls.append(call)
        count[entry] += 1
    if calls:
        if len(calls) < len(entries) and calls[-1].result != calls[-1].request:
            raise ValueError(f"{path}: incomplete final parent sequence")
        groups.append(tuple(calls))
    if set(count) != set(entries):
        raise ValueError(f"{path}: missing {[hex(entry) for entry in entries if not count[entry]]}")
    print(f"{path}: {len(groups)} parent calls, " +
          ", ".join(f"{entry:#x}={count[entry]}" for entry in entries))
    return groups


def summarize(groups):
    resolving_groups = []
    read_only = {}
    for group in groups:
        if any(call.resolves_slot() for call in group):
            resolving_groups.append(group)
        for call in group:
            if call.resolves_slot():
                continue
            slot = call.warm_slot()
            if not call.row_present:
                expected = 0x244db4da
            elif not call.back or slot < 0:
                expected = 0
            else:
                expected = call.input_word
            if call.before != call.after or call.result != expected:
                raise ValueError(f"Read-only row {call.entry:#x} changed cache or returned {call.result:#x}, expected {expected:#x}")
            # The warm body tests the header for zero and then reads only its cache slot.
            key = (call.entry, call.row_present, bool(call.back), call.before[0], slot, call.input_word)
            read_only[key] = call
    return resolving_groups, read_only


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("cpp", type=Path)
    parser.add_argument("--early", action="store_true", help="Compare the four earlier readers")
    args = parser.parse_args()
    original = read_groups(args.original, "original", early=args.early)
    cpp = read_groups(args.cpp, "cpp", early=args.early)
    original_writes, original_reads = summarize(original)
    cpp_writes, cpp_reads = summarize(cpp)
    if original_writes != cpp_writes or (not args.early and not original_writes):
        raise SystemExit("Ordered resolving groups differ or were not observed")
    if original_reads.keys() != cpp_reads.keys():
        raise SystemExit(f"Read-only inputs differ: {original_reads.keys() - cpp_reads.keys()} "
                         f"versus {cpp_reads.keys() - original_reads.keys()}")
    print(f"{len(original_writes)} ordered resolving groups and {len(original_reads)} read-only inputs matched")


if __name__ == "__main__":
    main()
