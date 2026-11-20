"""Compare the scoped height calls from two shovel gameplay routes."""
import argparse
from pathlib import Path
import re

from tests.acnh_shovel_target_replay import compare_live

ENTRIES = (0x15e026c, 0x83b024, 0x83a950, 0x241e98c, 0x60cba4, 0x862700, 0x22688e4)
CAST_ENTRIES = (0x83b024, 0x83a950, 0x862700, 0x87a980, 0x87aa70, 0x1285040)
TRACE = re.compile(
    r"acnh shovel height chain (original|cpp) entry=([0-9a-f]+)"
    r"(?: same_input=(?:stock|match))? (call=.* out=[0-9a-f]+)"
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("cpp", type=Path)
    parser.add_argument("--survey-tile", nargs=2, type=int, metavar=("X", "Z"))
    parser.add_argument("--entry", type=lambda value: int(value, 16), choices=tuple(dict.fromkeys((*ENTRIES, *CAST_ENTRIES))), action="append")
    parser.add_argument("--cast", action="store_true")
    args = parser.parse_args()

    for path in (args.original, args.cpp):
        for line in path.read_text(errors="replace").splitlines():
            if line.startswith("acnh shovel height chain ") and "same_input=mismatch" in line:
                raise SystemExit(f"{path}: {line}")
    compare_live((args.original, args.cpp), tuple(args.survey_tile) if args.survey_tile else None,
                 tuple(args.entry or (CAST_ENTRIES if args.cast else ENTRIES)), TRACE)


if __name__ == "__main__":
    main()
