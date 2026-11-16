"""Offline checked byte deltas. Runtime needs Zstandard, not Python or bsdiff.

NXDP0001 wraps BSDIFF-style additive/copy/seek instructions in one Zstd frame.
All integers are little endian; seek is signed two's complement. This container
is internal and versioned separately from the source-level feature registry.
"""
import bz2
import struct
import bsdiff4
import zstandard

MAGIC = b'NXDP0001'
LIMIT = 128 * 1024 * 1024


def signed_magnitude(data):
    value = int.from_bytes(data, 'little')
    return -(value & ((1 << 63)-1)) if value >> 63 else value


def create(original, replacement):
    if max(len(original), len(replacement)) > LIMIT:
        raise ValueError('Delta resource too large')
    legacy = bsdiff4.diff(original, replacement)
    control_size = signed_magnitude(legacy[8:16])
    diff_size = signed_magnitude(legacy[16:24])
    control = bz2.decompress(legacy[32:32+control_size])
    diff = bz2.decompress(legacy[32+control_size:32+control_size+diff_size])
    extra = bz2.decompress(legacy[32+control_size+diff_size:])
    if len(control) % 24:
        raise ValueError('Invalid generated control stream')
    triples = bytearray()
    for start in range(0, len(control), 24):
        copy, literal, seek = (signed_magnitude(control[start+i:start+i+8]) for i in (0, 8, 16))
        if min(copy, literal) < 0:
            raise ValueError('Invalid generated copy length')
        triples.extend(struct.pack('<QQq', copy, literal, seek))
    body = struct.pack('<QQQQ', len(replacement), len(triples), len(diff), len(extra)) + triples + diff + extra
    patch = MAGIC + zstandard.ZstdCompressor(level=12).compress(body)
    if apply(original, patch) != replacement:
        raise ValueError('Generated patch does not reconstruct reviewed output')
    return patch


def apply(original, patch):
    if not patch.startswith(MAGIC):
        raise ValueError('Unknown delta version')
    body = zstandard.ZstdDecompressor().decompress(patch[8:], max_output_size=3*LIMIT)
    if len(body) < 32:
        raise ValueError('Truncated delta header')
    size, controls, diffs, extras = struct.unpack_from('<QQQQ', body)
    if size > LIMIT or controls % 24 or 32+controls+diffs+extras != len(body) or diffs+extras != size:
        raise ValueError('Invalid delta lengths')
    diff_start, extra_start = 32+controls, 32+controls+diffs
    result = bytearray(); old_pos = diff_pos = extra_pos = 0
    for offset in range(32, 32+controls, 24):
        count, literal, seek = struct.unpack_from('<QQq', body, offset)
        if count > diffs-diff_pos or literal > extras-extra_pos or count+literal > size-len(result):
            raise ValueError('Invalid delta instruction')
        for i in range(count):
            old = original[old_pos+i] if 0 <= old_pos+i < len(original) else 0
            result.append((body[diff_start+diff_pos+i]+old) & 255)
        diff_pos += count; old_pos += count+seek
        result.extend(body[extra_start+extra_pos:extra_start+extra_pos+literal]); extra_pos += literal
    if len(result) != size or diff_pos != diffs or extra_pos != extras:
        raise ValueError('Incomplete delta')
    return bytes(result)
