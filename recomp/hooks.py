"""Composable instruction hook profiles, relative includes, and rebuild dependencies."""
from pathlib import Path

def hook_sources(path, active=None):
    path=Path(path).resolve(); active=set() if active is None else active
    if path in active:raise ValueError(f'Cyclic hook profile include: {path.name}')
    active=active|{path}; sources=[path]
    for raw in path.read_text().splitlines():
        line=raw.split('#',1)[0].strip()
        if line.startswith('include '):sources.extend(hook_sources(path.parent/line[8:].strip(),active))
    return sources

def read_hooks(path):
    hooks=set()
    for source in hook_sources(path):
        for raw in source.read_text().splitlines():
            line=raw.split('#',1)[0].strip()
            if line and not line.startswith('include '):hooks.add(int(line,16))
    return hooks

def read_dispatch(path):
    """Read opt-in function targets. Older importers treating directives as comments keep stock calls."""
    dispatch = set()
    for source in hook_sources(path):
        for raw in source.read_text().splitlines():
            directive = raw.strip()
            if directive.startswith("# dispatch "):
                address = directive[len("# dispatch "):].split("#", 1)[0]
                dispatch.add(int(address, 16))
    return dispatch


def read_resumes(path):
    """Read forward continuations from instruction hooks."""
    resumes = {}
    for source in hook_sources(path):
        for raw in source.read_text().splitlines():
            directive = raw.strip()
            if not directive.startswith("# resume "):
                continue
            start, stop = (int(word, 16) for word in directive[9:].split("#", 1)[0].split())
            if start < 0 or (start | stop) & 3 or start >= stop:
                raise ValueError(f"Invalid resume range {start:#x} -> {stop:#x} in {source.name}")
            if resumes.setdefault(start, stop) != stop:
                raise ValueError(f"Conflicting resume at {start:#x} in {source.name}")
    return resumes


def hooks_mtime(path):
    return max(p.stat().st_mtime for p in hook_sources(path))

def hooks_fingerprint(path):
    import hashlib
    h=hashlib.sha256()
    for p in hook_sources(path):
        h.update(p.name.encode()+b'\0'+p.read_bytes()+b'\0')
    return h.hexdigest()
