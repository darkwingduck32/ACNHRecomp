# 360 camera runtime

Enable `freecamera` in the Mod Manager and restart the game. The runtime currently
recognizes the ACNH 3.0.3 camera implementation by instruction fingerprints.

## Controls

During outdoor play the right stick (I/J/K/L with default keyboard bindings)
orbits immediately. Movement follows the camera direction. There is no camera
editing mode to enter or exit.

- Hold R3 and move the right stick vertically to zoom. Release to keep that zoom.
- Click and release R3 without moving the stick to recenter.
- On keyboard, hold F6 while using I/K to zoom; F7 recenters.
- The D-pad, L3, shoulders, triggers, and action buttons retain their game bindings.

The camera retains its orientation when leaving outdoor play. Active layout
cursors, dialogue page handlers, and text entry suspend camera input and movement
rotation; the regular game input then passes through. These signals do not yet
prove every overlay or cutscene is covered. Outdoor/menu/indoor transitions require
live verification before treating the control integration as complete.

## Visibility work in progress

The current implementation rotates the game's foreground/furniture rectangles
and terrain footprint with yaw. This fixes the direction of their long side but
is not a perspective-frustum culler and does not account for pitch or zoom.
The existing camera303 terrain scale is a separate local experiment.

The local parameter investigation identified `Param/Gfx/ActorDistantField.byml`
with `mDrumOffsetYOffset.f32`, `mDrumOffsetYScale.f32`, `mYScalePeakOffset.f32`,
`mRotateYParam.f32`, and `mScale.f32`. Their consumers still need tracing before
changing distant-scenery placement. Fog palette parameters exist separately;
increasing fog alone does not establish lower rendering cost or correct LOD.

Validation so far: camera/gesture unit tests and compilation of the affected
runtime, SDL input, and game UI hooks. Live playability, frustum coverage,
distant-scene grounding, LOD transitions, and performance remain open gates.
