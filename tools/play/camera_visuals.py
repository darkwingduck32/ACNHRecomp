#!/usr/bin/env python3
"""Check sky/fog behavior in an isolated SDR title run with 360 disabled.
Start the player with NX_LIVE_FRAME=1 and NX_FREECAMERA_TRACE=1. Test FOV-only,
zoom-only and default camera configurations separately. Review resulting images.
"""
import argparse
import json
import math
from pathlib import Path
import re
import camera_regression as regression
import live


def run(root, sky, fov):
    root=Path(root)
    result={'passed':False,'visual_review':'required','captures':[]}
    try:
        result['title']=regression.run(root,'title',enabled=False,samples=3)
        log=regression.read_log(root)
        markers=re.findall(r'camera: sky correction (\w+) bank (\d+) orbit (\d+) fov (\d+) zoom offset (\S+)',log)
        if sky:
            assert any(row[0]=='fce7966c1f0b7193' and row[1]=='1' and row[2]=='0' for row in markers), 'Sky color was not corrected with 360 off'
            assert any(row[0]=='3ebba4ffae3ffdd3' and row[2]=='0' for row in markers), 'Sky depth was not corrected with 360 off'
        else:
            assert not markers,'Default camera unexpectedly uses sky correction'
        projections=re.findall(r'perspective tan/aspect (\S+) (\S+)',log)
        assert projections
        tangent=float(projections[-1][0])
        assert math.tan(math.radians(fov)/2)-.002<=tangent<=math.tan(math.radians(fov+3)/2)+.002
        result['sky_patches']=markers
        for value in (0,1):
            live.configure(str(root),'camera.reduce_fog',value)
            live.wait_frames(str(root),120)
            assert f'camera: fog reduction {value} orbit 0' in regression.read_log(root),'Fog preference did not update with 360 off'
            assert not regression.state(root)['enabled']
            result['captures'].append(live.shot(str(root),f'fog-{value}',full=True))
        result['passed']=True
        return result
    finally:
        (root/'camera-visuals-result.json').write_text(json.dumps(result,indent=2)+'\n')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root');parser.add_argument('--sky',choices=('on','off'),required=True)
    parser.add_argument('--fov',type=int,default=31)
    args=parser.parse_args()
    print(json.dumps(run(live.root_of(args.root),args.sky=='on',args.fov),indent=2))
