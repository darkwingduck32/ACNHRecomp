"""Play the workspace's Windows build on a save copy and capture a performance report."""
import configparser
import csv
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import zipfile


REPO = Path(__file__).resolve().parents[1]
GAME = REPO / "private/game"
PLAYER = REPO / "private/acnh/build/acnh.exe"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save_hashes(folder):
    return {p.relative_to(folder): digest(p) for p in folder.rglob("*") if p.is_file()}


def prepare(root):
    save = GAME / "user/device_save"
    copy = root / "user/device_save"
    shutil.copytree(save, copy)
    if not (copy / "main.dat").is_file() or save_hashes(copy) != save_hashes(save):
        raise RuntimeError("The save changed during copying or is incomplete. Close the game and retry.")
    # The runtime writes config, shader caches, mod data and saves only in the test root.
    for name in ("romfs", "exefs", "spirv", "aoc", "game", "analysis"):
        if (GAME / name).exists():
            subprocess.run(["cmd", "/d", "/c", "mklink", "/J", str(root / name), str(GAME / name)], check=True)
    for name in ("cache", "mods"):
        if (GAME / name).exists():
            shutil.copytree(GAME / name, root / name)
    for p in [GAME / n for n in ("game.ini", "users.txt", "config.ini")] + list(GAME.glob("pipelines_*")):
        if p.is_file():
            shutil.copy2(p, root / p.name)


def main():
    reports = REPO / "private/performance-captures"
    reports.mkdir(exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix=time.strftime("%Y%m%d-%H%M%S-"), dir=reports))
    print(f"Preparing a playable save copy in {root}", flush=True)
    prepare(root)
    log = root / "runtime.log"
    env = dict(os.environ, NX_DATA_ROOT=str(root), NX_LOG=str(log), NX_MOD_FETCH="0", NX_DEBUG_FILES="0",
               NX_GPU_PROFILE="3", NX_FRAME_PROFILE="1", NX_SAMPLE="97",
               NX_SAMPLE_THREADS="replay,game main")
    metadata = {"player_sha256": digest(PLAYER), "start_local": time.strftime("%Y-%m-%d %H:%M:%S %z")}
    symbols = PLAYER.with_suffix(".map")
    if symbols.exists():
        shutil.copy2(symbols, root / "acnh.map")
    settings = root / "config.ini"
    # Include only settings relevant to rendering and gameplay.
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.read(settings, encoding="utf-8-sig")
    metadata["settings"] = {k: dict(cfg[k]) for k in ("video", "camera", "gameplay") if k in cfg}
    print("Press F8 during a dip to bookmark it. Keep playing for 20 seconds afterward.\n"
          "Close the diagnostic game normally to finish the report.", flush=True)
    start = time.monotonic()
    timeline = root / "timeline.csv"
    with timeline.open("w", newline="") as stream, (root / "console.log").open("wb") as console:
        writer = csv.writer(stream)
        writer.writerow(["elapsed_s", "runtime_log_bytes", "event"])
        process = subprocess.Popen([str(PLAYER)], env=env, cwd=root, stdout=console, stderr=subprocess.STDOUT)
        previous = False
        next_tick = start
        while process.poll() is None:
            now = time.monotonic()
            pressed = bool(ctypes.windll.user32.GetAsyncKeyState(0x77) & 0x8000)
            marked = pressed and not previous
            if now >= next_tick or marked:
                writer.writerow([round(now - start, 2), log.stat().st_size if log.exists() else 0,
                                 "F8" if marked else ""])
                stream.flush()
                next_tick = now + 1
            previous = pressed
            time.sleep(0.03)
    metadata["exit_code"] = process.returncode
    archive = root.with_suffix(".zip")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=3) as report:
        report.writestr("session.json", json.dumps(metadata, indent=2))
        for p in (log, timeline, root / "acnh.map", root / "console.log"):
            if p.is_file():
                report.write(p, p.name)
    print(f"Performance report: {archive}", flush=True)


if __name__ == "__main__":
    main()
