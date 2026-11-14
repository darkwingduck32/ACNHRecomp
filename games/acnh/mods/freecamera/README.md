# Free Camera (experimental)

ACNHRecomp runtime mod for ACNH 3.0.3. This requires a player built with the
freecamera runtime changes; it is not a standalone Switch/Atmosphere mod.

Copy this folder, including its empty `romfs` directory, to the game folder's
`mods/freecamera`. Enable **freecamera** in the launcher's Mod Manager and restart
the player. It can coexist with `camera303`; its pack is not copied or replaced.

| Action | Controller | Keyboard |
| --- | --- | --- |
| Toggle camera controls | R3 | F6 |
| Orbit / tilt while controls are on | Right stick | I/J/K/L |
| Zoom while controls are on | D-pad up/down | Up/down arrows |
| Reset view | L3 while controls are on | F7 |

The chosen view stays when camera controls are turned off. Movement follows
camera yaw. L/R and Q/E retain their normal functions.

The runtime scopes camera field changes to matrix construction to avoid feedback
into later camera updates. Field terrain and named foreground/furniture bounds
rotate with yaw. Hooks are restricted by known 3.0.3 instruction fingerprints.
Diagonal enclosing rectangles and low-angle performance still need refinement.

## Current tree status

Tree visuals are stock. Experimental mirrored, assembled and radial canopies
were rejected after in-game review and removed from the active mod. Rear-view
trees, fruit accommodation, and a visually acceptable 360-degree canopy remain
unresolved. No generated models or resource-size table are shipped here.

`tools/freecamera_trees.py` and `tools/assemble_freecamera_oak.py` preserve the
experimental work for further investigation. They are **not** part of launcher
startup or a verified asset pipeline. Do not generate into an active game mod
without reviewing the result separately. Their game-derived output belongs under
`private/`, which is ignored by Git.

## Validation

The player and launcher build successfully. Run the standalone camera tests with
`ctest --test-dir <build> -R '^freecamera$' --output-on-failure`. The optional tree
tool tests run with `python tests/freecamera_trees_test.py` and require
`zstandard`.

Controller input and camera-relative movement were confirmed in game. Bounds
rotation was checked through live runtime values; visual coverage/performance
across all scenes is not fully verified. Unit checks on rejected tree geometry
did not establish acceptable appearance.
