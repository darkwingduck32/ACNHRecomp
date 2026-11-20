"""Compare the stage height and field queries reached by the shovel collision probes."""
import argparse
from pathlib import Path
import re

from tests.acnh_shovel_target_replay import compare_live


ENTRIES = (0x15e06d0, 0x15e80f0, 0x15e7980, 0x195190c, 0x83a950, 0x862700)
TRACE = re.compile(
    r"acnh shovel (?:collision stage|height chain) (original|cpp) entry=([0-9a-f]+)"
    r"(?: same_input=(?:stock|match))? (call=.* out=[0-9a-f]+)"
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("cpp", type=Path)
    parser.add_argument("--survey-tile", nargs=2, type=int, metavar=("X", "Z"), required=True)
    args = parser.parse_args()
    paths = args.original, args.cpp
    for path in paths:
        for line in path.read_text(errors="replace").splitlines():
            if not line.startswith("acnh shovel height chain "):
                continue
            if "scope=9 " not in line or "same_input=mismatch" in line:
                raise SystemExit(f"{path}: {line}")
    compare_live(paths, tuple(args.survey_tile), ENTRIES, TRACE)


if __name__ == "__main__":
    main()
