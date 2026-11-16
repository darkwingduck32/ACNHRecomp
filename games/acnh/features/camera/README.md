# Native camera features

The camera is compiled into the player. It does not require `freecamera`,
`camera303`, or tool-durability folders at runtime. Its native module adapters,
resource transforms and renderer adapter register through the internal feature
surface described in [NATIVE_FEATURES.md](../../../../docs/NATIVE_FEATURES.md).

Both settings screens use these groups:

| Group | Options |
| --- | --- |
| Camera | Default FOV (20-80 degrees), default zoom (25-300%), reduce fog |
| 360 Camera | Enable orbit, orbit speed, zoom speed, vertical inversion |
| 360 Camera / Visual enhancements | Replace models, reduced glare |
| Gameplay enhancements | Extended tool durability |

Orbit enablement, sensitivity, inversion and lighting change live. Default zoom
applies on recenter, or directly while orbit is off. FOV still requires restart:
it enters native camera construction before culling and projection generation.
The title retains its native cinematic adjustment up to three degrees above the
base FOV. Room and UI cameras retain native controls and lenses.

Model replacement and tool durability are latched for the process because the
game caches these resources. Restart to apply changes to those choices. Changing
the 360 switch updates movement immediately; if it also changes the desired
model selection, settings show that the model change needs restart. Until then,
the loaded geometry and fruit attachment layout stay together.

Reviewed model patches and extended durability default on. Source distributions
include checked binary deltas and their manifest, and contain no complete model
files. The player reconstructs assets from the user's matching game data.

## Building

Normal CMake builds read `../deltas/manifest.json`, verify patch checksums and the
reviewed asset lock, then embed the delta package. Complete model inputs and
Python patch libraries are not needed for a normal source build.

Regenerating patches is a publisher operation. Install
`tools/requirements-native-features.txt`, then run
`python tools/build_native_features.py --game private/game --out private/native-features --export`.
This verifies private stock resources and reviewed replacement assets against
both existing locks before updating the distributable patch set. Complete models
stay private. Source profiles in `data/` contain camera parameter edits, fog
shader identifiers and fruit offsets.

The legacy package builder remains available for older players. New releases
use the native player and the existing native mod bundler; do not add the old
camera or durability ZIPs to their runtime mods inventory.

## Migration and coexistence

The launcher and runtime migrate saved legacy selections into `camera.enabled`
and `gameplay.extended_tool_durability`. Explicit native settings win. Existing
first-install markers preserve disabled choices. Only owned legacy IDs are
removed from `game.mods`; their files are retained for rollback. Native runtime
resolution ignores those retired camera packages, including recognized aliases.
SpeedFix, translation and unrelated mods remain separate and independently
managed. SpeedFix is neither embedded nor replaced by the camera implementation.

Resource adaptation follows external LayeredFS resolution. Camera parameters
apply checked scalar edits to a temporary copy of the chosen archive, retaining
unrelated changes. Model and durability replacements require exact reviewed
stock hashes. Different external replacements are skipped. Native models also
require the final compatible resource-size table; fruit geometry requires the
validated attachment adapter. Attachment updates preserve changes owned by
another hook. No stock or mod files are rewritten.

## Verification limits

The harness checks title culling, real pad orbit/zoom/reset, Settings and inventory
input gates, and room transitions using isolated saves. Native runs must omit all
camera/durability mod folders. Settings during loading and the original HDR/DLAA
configuration need separate regression runs. Visual review remains necessary;
structural assertions alone cannot detect rendering defects. The finite ocean
edge at extreme 32:9 / 80-degree / 300% views remains a known visual limit.

Sky centering, horizon correction and background-depth routing also apply with
360 disabled whenever FOV or zoom differs from the default. They do not depend
on replacement models. Reduce fog is a general Camera option, defaults on, and
updates immediately with 360 disabled. It reduces haze and dark-fog intensity
while retaining weather colors. The earlier softer-lighting preference migrates
to this setting; an explicit reduce-fog choice takes precedence.

On first use during loading, model patches apply to the player's own verified
stock resources. The native runtime decompresses, patches, checks decoded content,
recompresses, and stores a local cache under `cache/native-resources/v1` in the
game data root. Later launches validate and reuse it. Source game and mod files
remain unchanged. Corrupted cache entries regenerate; incompatible base resources
are skipped. Cache-write failure falls back to verified in-memory output.
The cache is local generated data and is never included in release bundles.
