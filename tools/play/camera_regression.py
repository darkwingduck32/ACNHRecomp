#!/usr/bin/env python3
"""Live camera regression checks; use an isolated save, NX_LIVE_FRAME=1 and
NX_FREECAMERA_TRACE=1. SDR frame dumps are required for visual evidence.

title: run at the title screen, without boot input queued.
controls: run after entering normal outdoor gameplay, with no menu open.
inactive: check that right-stick input leaves the camera unchanged indoors/off.
pitch-limits: hold each visible pitch limit and verify an immediate reversal.
dialogue: exercise typing, waiting, choices and return to outdoor control through
the native save dialogue; selects Keep playing and never saves/quits.
Reports contain assertions AND captures requiring human review; a successful
script alone does not establish that culling, models or lighting look correct.
"""
import argparse
import json
import math
from pathlib import Path
import re
import time

import live

INPUT = re.compile(r'freecamera: input frame (\d+) enabled ([01]) active ([01]) yaw ([-\d.]+) pitch ([-\d.]+) zoom ([-\d.]+)')


def usable_frame(path):
    from PIL import Image
    with Image.open(path) as image:
        pixels = image.convert('RGB').resize((64, 36)).tobytes()
    # A loading screen's small animated island is not evidence of rendered
    # terrain. Accept dark nighttime scenery; reject nearly solid black frames.
    return sum(max(pixels[i:i+3]) > 2 for i in range(0, len(pixels), 3)) >= len(pixels)/3*.2


def read_log(root):
    log = (root/'nx.log').read_text(errors='replace')
    if re.search(r'GPU hang|VK_ERROR_DEVICE_LOST|unhandled exception|fatal error', log, re.I):
        raise AssertionError('Runtime failure recorded; inspect nx.log')
    live.need_alive(str(root))
    return log


def state(root):
    samples = INPUT.findall(read_log(root))
    if not samples:
        raise AssertionError('No camera input trace: use the current build and NX_FREECAMERA_TRACE=1')
    frame, enabled, active, yaw, pitch, zoom = samples[-1]
    result = dict(frame=int(frame), enabled=bool(int(enabled)), active=bool(int(active)),
                  yaw=float(yaw), pitch=float(pitch), zoom=float(zoom))
    if live.frame(str(root))-result['frame'] > 90:
        raise AssertionError('Stale camera trace')
    return result


def assert_pose(a, b):
    for key in ('yaw', 'pitch', 'zoom'):
        if abs(a[key]-b[key]) > .0001:
            raise AssertionError(f'Camera {key} changed while gated: {a[key]} -> {b[key]}')


def wait_ready(root, timeout=90):
    deadline = time.monotonic()+timeout
    while live.frame(str(root)) < 600 or not INPUT.search(read_log(root)):
        if time.monotonic() > deadline:
            raise TimeoutError('Game did not become ready')
        time.sleep(.25)


def run(root, phase, enabled=True, scale=2, samples=4, reset_zoom=1, fov=31, inverted=False):
    root = Path(root).resolve()
    inverted_initial=inverted
    result = dict(phase=phase, assertions=[], captures=[], visual_review='required', passed=False)
    def check(condition, label):
        if not condition: raise AssertionError(label)
        result['assertions'].append(label)
    def capture(label):
        deadline = time.monotonic()+45
        while True:
            path = live.shot(str(root), phase+'-'+label, full=True)
            if usable_frame(path):
                result['captures'].append(path)
                return
            if time.monotonic() > deadline:
                raise AssertionError('Only blank/loading frames captured; visual checkpoint not reached')
            live.wait_frames(str(root), 120)
    def send(script):
        live.send(str(root), script)
        live.wait_frames(str(root), 60)
        return state(root)
    try:
        wait_ready(root)
        if phase == 'title':
            for n in range(samples):
                live.wait_frames(str(root), 180)
                capture(str(n+1))
            log = read_log(root)
            if enabled:
                check(f'terrain tile range {scale}x enabled' in log, 'Terrain profile installed')
                windows = re.findall(r'active terrain (\d+)x(\d+)', log)
                check(any(int(w) >= 18*scale and int(d) >= 6*scale for w,d in windows), 'Expanded terrain observed')
                projections = re.findall(r'perspective tan/aspect ([\d.]+) ([\d.]+)', log)
                check(bool(projections), 'Perspective visibility observed')
                expected = math.tan(math.radians(fov)/2)
                # The native title camera adds up to three degrees to its base
                # preset. Retain that cinematic adjustment and its culling lens.
                upper = math.tan(math.radians(fov+3)/2)
                check(expected-.002 <= float(projections[-1][0]) <= upper+.002,
                      'Configured FOV reaches native title visibility calculation')
                for family in (0, 1, 2, 3, 4):
                    check(f'visibility family {family} registered' in log, f'Visibility family {family} registered')
            else:
                check(not state(root)['enabled'], 'Feature is disabled')
        elif phase == 'inactive':
            before = state(root)
            check(not before['active'], 'Camera input is gated')
            # Native indoor controls still receive the stick. Return their yaw
            # before proceeding so subsequent door navigation remains repeatable.
            after = send('hold RRight 60; hold RLeft 60; press StickR')
            assert_pose(before, after)
            check(True, 'Right-stick input and recenter do not alter inactive camera')
            capture('gated')
        elif phase == 'pitch-limits':
            for inverted in (False,True):
                live.configure(str(root),'camera.invert_vertical',int(inverted))
                send('press StickR')
                down='RDown' if inverted else 'RUp'
                up='RUp' if inverted else 'RDown'
                high=send(f'hold {down} 240')
                check(high['active'],'Outdoor camera is active at pitch limit')
                capture('inverted-high' if inverted else 'high')
                reversed_high=send(f'hold {up} 8')
                check(reversed_high['pitch']<high['pitch']-.02,'Ground-facing pitch reverses within eight input frames')
                capture('inverted-high-reverse' if inverted else 'high-reverse')
                low=send(f'hold {up} 300')
                capture('inverted-low' if inverted else 'low')
                reversed_low=send(f'hold {down} 8')
                check(reversed_low['pitch']>low['pitch']+.02,'Horizon-facing pitch reverses within eight input frames')
                capture('inverted-low-reverse' if inverted else 'low-reverse')
            live.configure(str(root),'camera.invert_vertical',int(inverted_initial))
            send('press StickR')
        elif phase == 'dialogue':
            send('press StickR; hold RRight 20')
            pose=state(root)
            check(pose['active'],'Dialogue test starts with outdoor controls')
            start=live.frame(str(root))
            # Give the native page time to open before applying stick input.
            # This keeps the test about text animation, not transition latency.
            send('press Minus; hold RRight 120; hold StickR 30; at 140; hold RUp 30; wait 60')
            assert_pose(pose,state(root))
            check(not state(root)['active'],'Message owns input during typing and page wait')
            log=read_log(root)
            message=re.findall(r'acnh: message input frame (\d+) waiting ([01]) blocked ([01])',log)
            samples=[(int(f),int(wait),int(blocked)) for f,wait,blocked in message if int(f)>=start]
            check(any(not wait and not blocked for _,wait,blocked in samples),'Typed-text phase was observed before the page-turn button enabled')
            check(any(wait and not blocked for _,wait,blocked in samples),'Completed-page wait phase was observed')
            capture('page-wait')
            send('press A; hold RLeft 60; wait 45')
            assert_pose(pose,state(root))
            capture('choice')
            # The save confirmation's second item is Keep playing.
            send('press Down; press A; wait 180')
            check(state(root)['active'],'Outdoor input resumes after the dialogue closes')
            after=send('hold RRight 12')
            check(after['yaw']>pose['yaw']+.02,'Orbit responds after dialogue closes')
            capture('resumed')
            send('press StickR')
        else:
            initial = send('press StickR')
            check(initial['active'], 'Outdoor camera is active')
            check(abs(initial['yaw']) < .001 and abs(initial['pitch']) < .001 and abs(initial['zoom']-reset_zoom) < .001,
                  'Recenter uses configured distance and neutral orbit')
            capture('reset')
            right = send('hold RRight 30')
            check(right['yaw'] > .1, 'Right stick turns right')
            capture('right')
            left = send('hold RLeft 60')
            check(left['yaw'] < -.1, 'Left stick turns left')
            capture('left')
            send('press StickR')
            up = send('hold RUp 18')
            check(up['pitch'] * (-1 if inverted else 1) > .1, 'Vertical orbit follows configured direction')
            capture('pitch')
            live.settings(str(root), 'open')
            live.wait_frames(str(root), 60)
            before = state(root)
            check(not before['active'], 'Settings gate camera input')
            after = send('hold RRight 45')
            assert_pose(before, after)
            check(True, 'Settings navigation preserves camera pose')
            capture('settings')
            live.settings(str(root), 'close')
            send('press StickR')
            # The script cursor overlays both inputs, exercising the actual
            # modifier gesture rather than diagnostic initial-state overrides.
            zoom = send('hold StickR 30; at 0; hold RUp 30; wait 30')
            check(zoom['zoom'] < reset_zoom-.01, 'R3 and up zoom in')
            capture('zoom-in')
            zoom = send('hold StickR 60; at 0; hold RDown 60; wait 30')
            check(zoom['zoom'] > reset_zoom+.01, 'R3 and down zoom out')
            capture('zoom-out')
            end = send('press StickR')
            check(abs(end['yaw']) < .001 and abs(end['pitch']) < .001 and abs(end['zoom']-reset_zoom) < .001,
                  'Recenter restores configured pose after orbit and zoom')
            capture('restored')
        read_log(root)
        result['last_game_frame'] = live.frame(str(root))
        result['passed'] = True
    except BaseException as error:
        result['error'] = str(error)
        raise
    finally:
        (root/f'camera-{phase}-result.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root')
    parser.add_argument('phase', choices=('title', 'controls', 'inactive','pitch-limits','dialogue'))
    parser.add_argument('--disabled', action='store_true')
    parser.add_argument('--scale', type=int, default=2)
    parser.add_argument('--samples', type=int, default=4)
    parser.add_argument('--reset-zoom', type=float, default=1)
    parser.add_argument('--fov', type=float, default=31)
    parser.add_argument('--inverted', action='store_true')
    args = parser.parse_args()
    print(json.dumps(run(live.root_of(args.root), args.phase, not args.disabled, args.scale,
                         args.samples, args.reset_zoom, args.fov, args.inverted), indent=2))


if __name__ == '__main__': main()
