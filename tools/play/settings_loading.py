#!/usr/bin/env python3
"""Regression: launch an isolated live run, then open Settings at the first temporal frame.
Run this immediately after launch; the boot input must advance past the title.
usage: settings_loading.py <root> [--cycles 6] [--timeout 180]
"""
import argparse
import json
from pathlib import Path
import re
import time
import live

def temporal_count(log):
    return max((int(x) for x in re.findall(r'nvn/temporal: (\d+) frames',log)),default=0)

def run(root,cycles=6,timeout=180):
    root=Path(root).resolve();path=str(root)
    live.settings(path,'open-temporal')
    deadline=time.monotonic()+timeout
    while True:
        live.need_alive(path)
        log=(root/'nx.log').read_text(errors='replace')
        if 'GPU hang' in log:raise RuntimeError('GPU hang recorded during loading')
        opened=re.findall(r'ui: settings opened at game frame (\d+), temporal frames (\d+)',log)
        if not opened or opened[0][1]!='1':raise RuntimeError('Test was not armed before the first temporal frame; start a fresh run')
        if temporal_count(log)>=512:break
        if time.monotonic()>deadline:raise TimeoutError('Temporal rendering did not reach 512 frames')
        time.sleep(.25)
    events=[]
    for cycle in range(cycles):
        for action in ('close','open'):
            before=live.frame(path);ack=live.settings(path,action);after=live.wait_frames(path,30,timeout=30)
            events.append(dict(cycle=cycle+1,action=action,before=before,ack=ack,after=after))
    log=(root/'nx.log').read_text(errors='replace')
    if 'GPU hang' in log:raise RuntimeError('GPU hang recorded during settings cycling')
    live.need_alive(path)
    result=dict(settings_open_frame=int(opened[0][0]),settings_open_temporal_frame=1,
                temporal_frames_observed=temporal_count(log),last_game_frame=live.frame(path),events=events,gpu_hang=False)
    (root/'settings-loading-result.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root');p.add_argument('--cycles',type=int,default=6)
    p.add_argument('--timeout',type=float,default=180);a=p.parse_args()
    print(json.dumps(run(live.root_of(a.root),a.cycles,a.timeout),indent=2))
if __name__=='__main__':main()
