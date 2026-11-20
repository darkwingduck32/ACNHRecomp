"""Replay field-stage bounds and provider record lookup on the pinned AArch64 module."""
import argparse
from collections import Counter
from dataclasses import dataclass, field
from itertools import product
from pathlib import Path
import re
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2

from recomp import ehframe, nso
from tests.acnh_shovel_target_replay import compare_live
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
SIZES = {0x194f798: 0x268, 0x11eb21c: 0xe4}
FRAMES = {0x194f798: 0x10, 0x11eb21c: 0}
TRACE = re.compile(r"acnh shovel record leaf (original|cpp) entry=([0-9a-f]+) (call=.* out=[0-9a-f]+)")

BASE = 0x4000000000
P, X, R, TABLE, O, SLOT, STAGE = 0x20000, 0x21000, 0x22000, 0x23000, 0x24000, 0x25000, 0x26000

def branches():
    ptr = lambda n: struct.pack("<Q", n)
    for stage, channel, ready, tile in product(
            (0, 1, 56, 57, 0xffffffff), (0, 1, 7, 8), range(3),
            ((-6, -2), (-5, -2), (0, 0), (2, 1), (2, 2), (3, 1))):
        provider = bytearray(0xa0)
        struct.pack_into("<ii",provider,0x48,-5,-2)
        struct.pack_into("<ii",provider,0x58,2,1)
        struct.pack_into("<Q",provider,0x60+8*(channel if channel<8 else 0), 1 if ready==2 else 0)
        selector = 0x1f8 if stage==0 else 0x30+8*(stage if stage<57 else 0)
        regions = {
            BASE+0x4ab27a8:ptr(SLOT), SLOT:ptr(O), O+selector:ptr(P if ready else 0), P: bytes(provider),
            STAGE:struct.pack("<QIII",0,stage,0x12cafe,0),
            X:struct.pack("<Qii",0,*tile), BASE+0x5200388:bytes(0x30),
        }
        yield BASE,0x194f798,(STAGE,channel,X),regions

    for shape,tile,mode,root in product(
            ((3,4,12), (3,4,6), (0,4,12), (3,0,0)),
            ((-4, -7), (-3,-7), (-2,-6), (-2,-7), (-1,-4), (-1,-3)),range(5),(0,R)):
        width,stride,count=shape
        anchor_x,anchor_z=-3,-7
        data = bytearray()
        for i in range(12):
            item = 0xfffe if mode==1 else 0xfffd if mode>=2 else 0x102+i
            offsets = (((mode-2) & 0xff)<<16) | ((1 if mode==4 else 0)<<24)
            data.extend(struct.pack("<HBBI",item,1,3,offsets))
        provider = bytearray(0x30)
        struct.pack_into("<III",provider,8,width,stride,count)
        struct.pack_into("<Q",provider,0x18,TABLE)
        struct.pack_into("<ii",provider,0x28,anchor_x,anchor_z)
        regions = {
            BASE+0x4ab37e0:ptr(0), P:bytes(provider), TABLE:bytes(data),
            X:struct.pack("<Qii",0,*tile), R:struct.pack("<Qii",0,-231,402),
        }
        yield BASE,0x11eb21c,(P,X,root),regions


@dataclass
class Snapshot:
    base: int
    entry: int
    sp: int
    args: tuple
    memory: dict = field(default_factory=dict)
    exit: dict = field(default_factory=dict)
    returned: int | None = None

    def place(self, address, data):
        for offset, byte in enumerate(data):
            self.memory.setdefault(address + offset, byte)

    def regions(self):
        blocks = {}
        previous = None
        for address, value in sorted(self.memory.items()):
            if previous is None or address != previous + 1:
                anchor = address
                blocks[anchor] = bytearray()
            blocks[anchor].append(value)
            previous = address
        return {address: bytes(data) for address, data in blocks.items()}


def read_capture(path):
    captures = {}
    for line in path.read_text(errors="replace").splitlines():
        if not line.startswith("HEIGHT_"):
            continue
        tag, key, *values = line.split()
        key = int(key, 16)
        entry = key >> 32
        if entry not in SIZES:
            continue
        if tag == "HEIGHT_BEGIN":
            base, _, sp, *args = (int(value, 16) for value in values)
            captures[key] = Snapshot(base, entry, sp, tuple(args[:3]))
            continue
        captured = captures[key]
        if tag == "HEIGHT_BYTES":
            phase, _, address, data = values
            address = int(address, 16)
            data = bytes.fromhex(data)
            if phase == "arg":
                captured.place(address, data)
            elif phase == "exit":
                captured.exit[address] = data
        elif tag == "HEIGHT_LOAD":
            address, width, value = values
            address, width, value = int(address, 16), int(width), int(value, 16)
            if not captured.sp - FRAMES[entry] <= address < captured.sp:
                captured.place(address, (value % (1 << (8*width))).to_bytes(width, "little"))
        elif tag == "HEIGHT_END":
            captured.returned = int(values[0], 16)
    return captures.values()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--entry", type=lambda raw: int(raw, 16), choices=tuple(SIZES))
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    parser.add_argument("--survey-tile", nargs=2, type=int, metavar=("X", "Z"))
    parser.add_argument("--no-op", action="store_true")
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    extents = {fn.start: fn.size for fn in ehframe.parse(module)}
    if any(extents.get(entry) != size for entry, size in SIZES.items()):
        parser.error("unexpected record lookup function boundary")
    entries = tuple(SIZES) if args.entry is None else (args.entry,)
    compare_live(args.live, tuple(args.survey_tile) if args.survey_tile else None, entries, TRACE)
    cases = [(*case, None) for case in branches() if case[1] in entries]
    if args.capture:
        observed = [cap for cap in read_capture(args.capture) if cap.entry in entries]
        if not observed:
            raise SystemExit("no matching captured record calls")
        cases.extend((cap.base, cap.entry, cap.args, cap.regions(), cap) for cap in observed)

    payload = []
    for base, entry, regs, memory, _ in cases:
        pairs = [(addr + i, byte) for addr, span in memory.items() for i, byte in enumerate(span)]
        payload.append(" ".join(map(str, [base, entry, *regs, len(pairs), *(n for pair in pairs for n in pair)])))
    command = [str(args.candidate.resolve()), *(["no-op"] if args.no_op else [])]
    proc = subprocess.run(command, input="\n".join(payload) + "\n", text=True, capture_output=True, timeout=45)
    if proc.returncode:
        raise SystemExit(proc.stderr)
    native = proc.stdout.splitlines()
    if len(native) != len(cases):
        raise ValueError("candidate returned the wrong number of cases")

    counts = Counter()
    for n, ((base, entry, regs, memory, actual), line) in enumerate(zip(cases, native), 1):
        inputs = dict(zip((UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2), regs))
        code = bytes(module.image[entry:entry + SIZES[entry]])
        first = run(base + entry, code, inputs, (UC_ARM64_REG_X0,), memory)
        if first != run(base + entry, code, inputs, (UC_ARM64_REG_X0,), memory):
            raise ValueError(f"unstable reference case {n}")
        expected = [first.registers[UC_ARM64_REG_X0], *(v for write in first.writes for v in write)]
        if actual:
            if expected[0] != actual.returned:
                raise ValueError(f"capture return differs in case {n}")
            for address, data in actual.exit.items():
                start, after = next((a, d) for a, d in first.memory.items() if a <= address and address + len(data) <= a + len(d))
                if after[address-start:address-start+len(data)] != data:
                    raise ValueError(f"capture output differs in case {n}")
        if expected != list(map(int, line.split())):
            raise ValueError(f"case {n} at {entry:#x}: original {expected}; C++ {line}")
        counts[entry] += 1
    for entry, count in sorted(counts.items()):
        print(f"{entry:#x}: {count} cases match both original AArch64 runs")


if __name__ == "__main__":
    main()
