#!/usr/bin/env python3
"""Generate portable C++ immutable resources from a checked build manifest.
The manifest and generated payloads belong in a private build directory. Only
logical names, hashes and resource bytes are embedded, never source file paths.
"""
import argparse
import hashlib
import json
from pathlib import Path


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write_changed(path, text):
    if not path.exists() or path.read_text(encoding='utf-8') != text:
        path.write_text(text, encoding='utf-8')


def generate(records, output, require_delta_models=False):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    entries = []
    payloads = {}
    identities = set()
    total = 0
    for record in records:
        feature, path = record['feature'], record['path']
        if not feature or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789._-' for c in feature):
            raise ValueError('Invalid resource owner')
        if '\\' in path or ':' in path or any(ord(c)<32 for c in path) or any(part in ('', '.', '..') for part in path.split('/')):
            raise ValueError('Unsafe logical resource path')
        if (feature, path) in identities:
            raise ValueError('Duplicate resource ownership')
        identities.add((feature, path))
        encoding=record.get('encoding','literal')
        if record.get('prebuilt'):
            if encoding not in ('delta','zstd-delta'):
                raise ValueError('Only deltas may use a prebuilt catalog')
            for key in ('input_sha256','output_sha256','content_sha256','payload_sha256'):
                value=record.get(key,'')
                if len(value)!=64 or any(c not in '0123456789abcdef' for c in value):
                    raise ValueError('Invalid prebuilt resource hash')
            if not 0<=record.get('content_size',-1)<=128*1024*1024:
                raise ValueError('Invalid prebuilt resource size')
            replacement=b''
        else:
            original = Path(record['input']).read_bytes()
            replacement = Path(record['replacement']).read_bytes()
            if digest(original) != record['input_sha256'] or digest(replacement) != record['output_sha256']:
                raise ValueError(f'Resource input changed: {feature}/{path}')
            if max(len(original), len(replacement)) > 128*1024*1024:
                raise ValueError('Resource exceeds native transform limit')
        total += record['content_size'] if record.get('prebuilt') else len(replacement)
        if total > 512*1024*1024:
            raise ValueError('Embedded resources exceed build limit')
        if encoding not in ('literal','delta','zstd-delta'):
            raise ValueError('Unknown embedded resource encoding')
        payload=replacement if encoding=='literal' else Path(record['payload']).read_bytes()
        payload_hash=digest(payload)
        if encoding!='literal' and (payload_hash!=record['payload_sha256'] or not payload.startswith(b'NXDP0001')):
            raise ValueError('Invalid native delta payload')
        # Model resources may never silently fall back to full embedded assets.
        if require_delta_models and path.lower().startswith('model/') and encoding!='zstd-delta':
            raise ValueError('Model resources must be distributed as decoded deltas')
        name = 'asset_' + payload_hash
        payloads[name] = payload
        entries.append((feature, path, record['input_sha256'], record['output_sha256'], name,encoding,
                        record.get('content_sha256',''),record.get('content_size',0)))
    active = {'index.cpp'}
    for name, data in sorted(payloads.items()):
        file = name + '.cpp'
        active.add(file)
        chunks = []
        for start in range(0, len(data), 4096):
            chunk = data[start:start+4096]
            literal = '\n'.join('"'+''.join(f'\\x{byte:02x}' for byte in chunk[i:i+48])+'"'
                                for i in range(0, len(chunk), 48))
            chunks.append(f'std::string_view({literal}, {len(chunk)})')
        literals = ',\n'.join(chunks) or 'std::string_view{}'
        # Bounded literals work with MSVC as well as clang/GCC. Assemble once;
        # the resulting immutable views have process lifetime and support NULs.
        write_changed(output/file, '#include <string>\n#include <string_view>\nnamespace nx::features::generated {\n'
                      f'std::string_view {name}() {{\nstatic const std::string bytes = [] {{\n'
                      f'std::string value; value.reserve({len(data)});\n'
                      f'for (auto chunk : {{{literals}}}) value.append(chunk);\nreturn value;\n}}();\nreturn bytes;\n}}\n}}\n')
    declarations = ''.join(f'std::string_view {name}();\n' for name in sorted(payloads))
    encodings={'literal':'Literal','delta':'Delta','zstd-delta':'ZstdDelta'}
    rows = ',\n'.join('{' + ','.join([*(json.dumps(x) for x in item[:4]), f'generated::{item[4]}()',
                                    'ResourceEncoding::'+encodings[item[5]],json.dumps(item[6]),str(item[7])]) + '}'
                       for item in sorted(entries))
    body = ('static const EmbeddedResource data[] = {\n'+rows+'\n};\nreturn data;' if entries else 'return {};')
    write_changed(output/'index.cpp', '#include "features/embedded_resources.h"\n'
                  'namespace nx::features::generated {\n'+declarations+'}\n'
                  'namespace nx::features {\nstd::span<const EmbeddedResource> embedded_resources() {\n'+body+'\n}\n}\n')
    # Only remove this generator's own stale source files, within its resolved output.
    for path in output.glob('asset_*.cpp'):
        if path.name not in active:
            assert path.resolve().parent == output.resolve()
            path.unlink()
    return len(entries), total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    records = json.loads(args.manifest.read_text())
    count, size = generate(records, args.output)
    print(f'Embedded {count} checked resources ({size} payload bytes)')


if __name__ == '__main__':
    main()
