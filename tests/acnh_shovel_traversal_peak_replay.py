"""Replay the traversal peak with explicit surface-height callbacks."""
import argparse
from pathlib import Path
import re
import struct
import subprocess

from unicorn.arm64_const import (
    UC_ARM64_REG_S0, UC_ARM64_REG_S1, UC_ARM64_REG_S2, UC_ARM64_REG_X0,
)

from recomp import ehframe, nso
from tests.acnh_provider_cell_replay import address_words, words, RETURN
from tests.function_replay import run
from tools.acnh_version import build_id


BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, ENTRY, SIZE = 0x4000000000, 0x15f27e0, 0x358
ORIGIN, TAPE = 0x21000, 0x22000
HEIGHT = 0x15e026c
CALLS = (0x15f2810, 0x15f2844, 0x15f2898, 0x15f28ec, 0x15f2940,
         0x15f2994, 0x15f29e8, 0x15f2a48, 0x15f2a9c, 0x15f2b00)
SCALARS = (UC_ARM64_REG_S0, UC_ARM64_REG_S1, UC_ARM64_REG_S2)
SAMPLE_TRACE = re.compile(
    r"acnh shovel traversal sample (original|cpp) call=([0-9a-f]+)"
    r" before=([0-9a-f,]+) after=([0-9a-f,]+) out=([0-9a-f]+)"
)
PEAK_TRACE = re.compile(
    r"acnh shovel traversal peak (original|cpp) entry=15f27e0 call=15f11e0"
    r" input=([0-9a-f,]+) out=([0-9a-f,]+)"
)


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def xyz(x, y, z):
    return bits(x), bits(y), bits(z)


def callback():
    def mem(op, target, base, offset, width=4):
        return op | (offset // width << 10) | (base << 5) | target

    read_word, write_word = 0xb9400000, 0xb9000000
    record = [
        *address_words(9, TAPE),
        mem(read_word, 10, 9, 0),  # ldr w10, [x9]
        0x9101012b,  # add x11, x9, #64
        0x8b0a196b,  # add x11, x11, x10, lsl #6
    ]
    for offset in (0, 4, 8):
        record += [mem(read_word, 12, 0, offset), mem(write_word, 12, 11, offset + 20)]
    record += [
        mem(0xf9000000, 1, 11, 32, 8),  # str x1
        mem(0xf9000000, 2, 11, 40, 8),  # str x2
        mem(0xbd000000, 0, 11, 48),  # str s0
        mem(read_word, 12, 11, 4),
        0x340000ec,  # cbz w12, after the six write instructions
    ]
    for offset in (0, 4, 8):
        record += [mem(read_word, 12, 11, offset + 8), mem(write_word, 12, 0, offset)]
    record += [
        mem(0xbd400000, 0, 11, 0),  # ldr s0
        0x1100054a,  # add w10, w10, #1
        mem(write_word, 10, 9, 0),
        RETURN,
    ]
    return words(*record)


def fixtures():
    for origin in (xyz(405, 15, 685), xyz(-6, -0., 14), xyz(0, -0., -0.)):
        for highest in range(10):
            for mutate in (False, True):
                replies = tuple((bits(23 if i == highest else 12), int(mutate),
                                 *xyz(400 + i, -5, 678 + 2*i)) for i in range(10))
                yield origin, replies
        for heights in ([4.] * 10, list(range(10)), list(reversed(range(10))),
                        [float("nan")] + [15.] * 9, [0., float("inf")] + [5.] * 8):
            yield origin, tuple((bits(value), 0, 0, 0, 0) for value in heights)


def read_peaks(path, route):
    def components(text):
        values = tuple(int(component, 16) for component in text.split(","))
        if len(values) != 3:
            raise ValueError(f"{path}: expected three coordinate bits")
        return values

    samples = []
    peaks = []
    for line in path.read_text(errors="replace").splitlines():
        if match := SAMPLE_TRACE.fullmatch(line):
            if match[1] != route:
                raise ValueError(f"{path}: unexpected sample route {match[1]}")
            samples.append((int(match[2], 16), components(match[3]), components(match[4]), int(match[5], 16)))
        elif match := PEAK_TRACE.fullmatch(line):
            if match[1] != route or tuple(sample[0] for sample in samples) != CALLS:
                raise ValueError(f"{path}: unexpected peak route or sample order")
            origin = components(match[2])
            if samples[0][1] != origin:
                raise ValueError(f"{path}: the first height call did not receive the origin")
            peaks.append((origin, tuple(samples), components(match[3])))
            samples.clear()
    if samples or not peaks:
        raise ValueError(f"{path}: incomplete traversal peak trace")
    print(f"{path}: {len(peaks)} peaks and {len(peaks) * len(CALLS)} ordered surface-height calls")
    return peaks


def run_original(code, origin, replies):
    shim = callback()
    callees = {BASE + site: (BASE + HEIGHT, shim) for site in CALLS}
    tape = bytearray(64 * (len(CALLS) + 1))
    for index, reply in enumerate(replies, 1):
        struct.pack_into("<5I", tape, index * 64, *reply)
    memory = {ORIGIN: struct.pack("<3I", *origin), TAPE: bytes(tape)}
    result = run(BASE + ENTRY, code, {UC_ARM64_REG_X0: ORIGIN, UC_ARM64_REG_S0: bits(3)},
                 SCALARS, memory, callees=callees)
    if len(result.calls) != len(CALLS) or result.calls != tuple((BASE + site, BASE + HEIGHT,
                              ORIGIN if index == 0 else result.calls[1][2])
                             for index, site in enumerate(CALLS)):
        raise ValueError("unexpected callback sites or point addresses")
    tape = result.memory[TAPE]
    if struct.unpack_from("<I", tape)[0] != len(CALLS):
        raise ValueError("unexpected number of height calls")
    inputs = []
    for index in range(1, len(CALLS) + 1):
        offset = index * 64
        inputs.extend(struct.unpack_from("<3I", tape, offset + 20))
        if struct.unpack_from("<QQI", tape, offset + 32) != (0, 0, 0):
            raise ValueError("unexpected height callback ABI")
    writes = [write for write in result.writes if ORIGIN <= write[0] < ORIGIN + 12]
    expected_writes = [(ORIGIN + 4*i, 4, value) for i, value in enumerate(replies[0][2:])] if replies[0][1] else []
    if writes != expected_writes:
        raise ValueError("unexpected caller-visible writes")
    return (*result.registers.values(), *struct.unpack("<3I", result.memory[ORIGIN]), *inputs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--capture", type=Path, help="Replay peak inputs from the original gameplay trace")
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    if not any(function.start == ENTRY and function.size == SIZE for function in ehframe.parse(module)):
        parser.error("unexpected traversal peak boundary")
    code = bytes(module.image[ENTRY:ENTRY + SIZE])
    cases = list(fixtures())
    portable = len(cases)
    captures = read_peaks(args.capture, "original") if args.capture else []
    if args.live:
        original, native = args.live
        if read_peaks(original, "original") != read_peaks(native, "cpp"):
            raise SystemExit("Traversal peak gameplay traces differ")
    for origin, samples, _ in captures:
        cases.append((origin, tuple((height, int(before != after), *after)
                                    for _, before, after, height in samples)))
    payload = "\n".join(" ".join(map(str, (*origin, *(value for reply in replies for value in reply))))
                        for origin, replies in cases) + "\n"
    proc = subprocess.run([str(args.candidate.resolve())], input=payload, text=True,
                          capture_output=True, timeout=30, check=True)
    actual = proc.stdout.splitlines()
    if len(actual) != len(cases):
        raise SystemExit("candidate returned the wrong number of rows")
    for index, ((origin, replies), line) in enumerate(zip(cases, actual)):
        expected = run_original(code, origin, replies)
        if expected != run_original(code, origin, replies):
            raise SystemExit(f"unstable original in case {index}")
        candidate = tuple(map(int, line.split()))
        if expected != candidate:
            raise SystemExit(f"case {index}: expected {expected}, got {candidate}")
        if index >= portable:
            _, samples, vector = captures[index - portable]
            captured = (*vector, *samples[0][2], *(value for _, point, _, _ in samples for value in point))
            if expected != captured:
                raise SystemExit(f"case {index}: original AArch64 differs from the live capture")
    print(f"{len(cases)} traversal peak cases match both original AArch64 runs")


if __name__ == "__main__":
    main()
