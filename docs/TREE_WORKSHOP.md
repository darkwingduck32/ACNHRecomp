# Tree foliage iteration

## Material face visibility

The viewer and software renderer now read `gsys_render_state_display_face` from
each shape's indexed BFRES v9 material. The default is the stored front/back/both
setting, not a universal front-only mode. **Force all backfaces** overrides this
for diagnosis; leave it unchecked for asset material settings. Missing metadata
falls back to front faces; catalog validation requires explicit settings on all
current shapes. These are asset settings, not a captured runtime draw-state trace.

For example, mature snow oak leaves and interior materials specify `both`, while
snow and trunk specify `front`. Earlier screenshots incorrectly culled those leaf
backfaces. Updated turnarounds are `forest-v6/Oak-material-faces.png` and
`forest-v6/Bamboo-material-faces.png`. No model geometry changes accompany this
viewer correction. The v6 branch composition remains lighter than v5.

Layout reference: [BfresLibrary MaterialParser](https://github.com/KillzXGaming/BfresLibrary/blob/master/BfresLibrary/Switch/Model/MaterialParser.cs);
face semantics: [Track Studio material documentation](https://mapstudioproject.github.io/TrackStudioDocs/bfres/Materials.html).

## Art-directed snow assemblies (forest-v6)

The review UI now loads v6; **Previous review** selects v5. Review notes from
`tree-reviews-2.json` remain available. Only the OakSnow and BambooSnow archives
change from v5: growth stages 3 and 4 and their Node counterparts (eight models).

`tools/tree_art_direction.py` defines the branch placements explicitly. The oak
uses two front shoulders, one slightly raised rear shoulder, and an offset upper
leader. Each assembly carries the original scalloped snow parts, hanging leaf
fringe and interior geometry together. The original snow primitives, dimensions,
UVs and material are preserved. Rear foliage wraps around each branch's pivot.
These models bypass the scatter, crown closure and generated snow-deposit paths.

Bamboo retains its original two sculpted snow layers and ascending leaf sprays.
Rear and selected side sprays fill out the upper canopy. Low sprays stay on their
original stalks and receive reversed faces/normals so they remain visible from
behind. No snow geometry is added to stage 3, which originally has frosted foliage
without separate snow masses.

The source parts are still assembled by code, but composition is a small editable
set of deliberate placements rather than a density/coverage optimization. Review
turnarounds at normal and high elevation under `private/freecamera/forest-v6`.
Snow retains the substantial volume of the original art; whether that treatment
is preferred is a visual decision. Render lighting is approximate, and validation
does not establish correct procedural wind or collision behavior in the game.

Reviewed representative young snow oaks, snow cedar/decorated cedar, snow palm,
huge tree, blossom and museum oak alongside the changed models. Their current
shapes were retained in this snow-focused pass. The blossom and museum foliage
still have some visible card layering; they can use a separate composition pass.

```powershell
private/venv/Scripts/python.exe tools/generate_forest.py --out private/freecamera/forest-v6
private/venv/Scripts/python.exe tools/build_tree_catalog.py
private/venv/Scripts/python.exe -m unittest discover -s tests -p '*_test.py'
node tests/tree_catalog_runtime_test.cjs
```

## Second review revision (forest-v5)

The review UI now compares v5 with v4 and imports `tree-reviews-2.json`.
Blossom and museum crowns use density 3 instead of 6; ordinary oaks use 4.
Some of that existing budget is distributed inside the crown tiers to reduce
the hollow-ring appearance without adding more clusters. Small closure clusters
fit inside the upper crown and stay below the original maximum height. The huge
tree uses rotated native vine sectors with no upward-facing vine closure.

The broad continuous snow domes from v4 have been removed. Snow is now generated
as separate, leaf-scale deposits supported by the generated foliage. Each patch
has an opaque texture sample at its center, lies on one connected leaf cluster,
and has at least half of its rim above opaque leaf texels. The patch can bridge
small gaps between leaflets; it cannot span an entire crown. Frosted leaf textures
provide the rest of the winter coverage. This intentionally gives a lighter snow
treatment than the original large snow masses and needs visual review.

```powershell
private/venv/Scripts/python.exe tools/generate_forest.py --out private/freecamera/forest-v5
private/venv/Scripts/python.exe tools/build_tree_catalog.py --forest private/freecamera/forest-v5 --previous private/freecamera/forest-v4 --reviews private/freecamera/tree-review/tree-reviews-2.json
```

Snow tests now also limit each deposit's footprint to less than two model units,
require closed geometry, and check that every vertex has a supporting leaf
surface less than 0.38 units below it. Review at normal, high, and overhead camera
angles; horizontal coverage alone does not validate crown shape.

## Crown and snow revision (forest-v4)

The current review UI loads `forest-v4` and offers **Original game** or
**Previous review** (`forest-v3`) as the left-hand comparison. Exported notes from
`private/freecamera/tree-review/tree-reviews.json` seed the review controls.
Changed hashes reset approval to Unreviewed while retaining the note. The original
review export and the previous generated models remain available.

V4 adds upward-facing clusters across the centers of layered crowns, reduces the
young blossom crown from density 6 to 4 with an irregular radial outline, and
redistributes snow bamboo clusters. Directional oak/bamboo snow is rebuilt as
continuous closed domes with irregular rolled edges instead of overlapping
rotated copies. The snow retains the native material and nearest native skin
assignments. Its appearance is a candidate for review, not an exact recreation of
Nintendo's snow geometry. Eleven archives changed; cedar, palm, and other retained
models have the same output hashes as the prior review.

```powershell
private/venv/Scripts/python.exe tools/generate_forest.py --out private/freecamera/forest-v4
private/venv/Scripts/python.exe tools/build_tree_catalog.py --forest private/freecamera/forest-v4 --previous private/freecamera/forest-v3
```

Validate overhead views as well as horizontal turnarounds:

```powershell
private/venv/Scripts/python.exe tools/render_tree_views.py --before private/freecamera/forest-v3/romfs/Model/PltTreeOak.Nin_NX_NVN.zs --before-label "Previous review" --after private/freecamera/forest-v4/romfs/Model/PltTreeOak.Nin_NX_NVN.zs --model PltTreeOak4Node --elevation 90 --output private/freecamera/forest-v4/oak-top.png
```

Snow geometry checks require every mesh edge to have exactly two incident
triangles and reject degenerate triangles. Crown closure checks require upward
cluster normals. The catalog runtime check also exercises previous/stock baseline
switching without losing the selected model.

## Review every tree in one UI

Open `private/freecamera/tree-review/index.html`. Keep its `assets/` folder beside
it; models load from local scripts, so no web server or Internet connection is
needed. Select a tree asset and then its growth/seasonal/reaction model. Cameras,
lighting, shape toggles, and animation playback stay synchronized between stock
and candidate. The status text identifies models whose geometry was retained.
Reviews and notes are stored in the browser when local storage is available;
use **Export reviews** to save a portable JSON copy.

Build all tree candidates and the review catalog:

```powershell
private/venv/Scripts/python.exe tools/generate_forest.py --out private/freecamera/forest-v3
private/venv/Scripts/python.exe tools/build_tree_catalog.py --forest private/freecamera/forest-v3
```

Use a fresh output folder for another generation. Add `--scan` to refresh discovery
after changing the game dump. Discovery scans environment, plant, and furniture
resources for tree foliage references, then includes the explicitly named tree
assets. Its ignored local cache is under `private/freecamera/`.

The current catalog includes 38 assets and 156 models. All island oak, blossom,
snow oak, cedar, decorated cedar, palm, and bamboo files are included. It also
includes tree props, museum tree models, and the tree-bearing models within the
insect museum scene. Non-tree plants in shared scenes, tree-themed clothing,
wallpaper, icons, and a wreath reusing oak textures are excluded. Seasonal
recolors using the same geometry inherit their base geometry changes.

The combined mod candidate is `private/freecamera/forest-v3/romfs`. Its report
lists every changed surface, retained asset, source hash, and resource heap change.
Twenty-one archives are modified. Solid/decorative assets without targeted leaf
surfaces are retained and shown alongside their unchanged original. No candidate
is installed or enabled automatically.

Broadleaf crowns use the approved layered radial construction. Directional bamboo
branches are replicated around the trunk. Palms and cedars retain their authored
arrangements and receive reversed faces with corrected normals/tangent handedness.
Directional snow caps receive a rotated rear copy; caps already surrounding the
crown are retained. Native vertex weights, bone palettes, UVs, materials, embedded
textures, and skeletons are retained. Moving weighted foliage preserves animation
bindings but still requires an aesthetic animation review.

The catalog now supports weighted skeletons, seasonal clip matching, animated
scale, and the actual blossom/snow albedo textures. Shading still approximates the
game. Procedural wind, runtime animation blending, and collision physics remain
outside the viewer. Island performance and final shader appearance need in-game
validation after review.

Validate geometry and the catalog controller:

```powershell
private/venv/Scripts/python.exe -m unittest discover -s tests -p '*oak_test.py'
private/venv/Scripts/python.exe -m unittest discover -s tests -p forest_test.py
node tests/tree_catalog_runtime_test.cjs
```

The Node check uses the real generated data, controller, and Three.js geometry
with a stub renderer: it checks every model/clip for finite posed geometry and
review-state persistence. It is not a WebGL/browser visual test. Software
turnarounds provide separate visual checks without browser automation.

## Iterate one oak

The local tree tools generate experimental oak geometry and preview it without
launching the game. Game-derived assets and images stay under ignored `private/`.
The tools require the existing Python environment plus Pillow and NumPy.

Generate a candidate, an offline interactive comparison, and an eight-angle render:

```powershell
private/venv/Scripts/python.exe tools/generate_round_oak.py --density 6 --depth 0.95 --seed 17 --out private/freecamera/round-oak-next --preview --render
```

Open `preview.html` or `turnaround.png` in that output folder. Regeneration takes
seconds. Use a new output folder to retain each iteration. Density controls the
number of complete native leaf clusters; depth controls canopy depth relative to
width; seed changes their distribution. The default layered style revolves the
stock crown's horizontal outline and preserves each cluster's height and droop.
`--style sphere` is available as a comparison, but gives an overly spherical crown.

The first reviewed candidates are `round-oak-v1` (spherical), `round-oak-v2`
(layered, density 2), `round-oak-v3` (density 4), and `round-oak-v4` (density 6).
V4 is the fuller starting point; V3 is the cheaper geometry option. These are
local artifacts, not files distributed with the repository.

Only growth forms `PltTreeOak3` and `PltTreeOak4`, including their corresponding
`Node` reaction models, are modified. The old LeafBack underside is disabled.
Complete original clusters are placed around the crown; UVs, texture files,
skeletons, material records, and animation archives are retained. Positions,
normals, tangents, and per-vertex wind pivots are transformed together. The resource
heap table is enlarged for the new buffers. Other shapes are checked for unchanged
vertex/index data. Output is a separate RomFS mod candidate; generation does not
install or enable it in the game.

In the viewer, choose a `Node` model and a game shake/cut clip to scrub or play its
skeletal reaction. Render a particular reaction pose with:

```powershell
private/venv/Scripts/python.exe tools/render_tree_views.py --after private/freecamera/round-oak-v4/romfs/Model/PltTreeOak.Nin_NX_NVN.zs --clip PltTreeOak4ShakeL --frame 12 --output private/freecamera/round-oak-v4/shake-frame12.png
```

The software renders use real opacity/grayscale textures, original vertex normals,
front-face culling, a depth buffer, and approximate lighting/tints. The JSON beside
each contact sheet records visible foliage pixel counts for each angle. These
counts measure directional coverage consistency, not resemblance to the stock
crown or game performance. Neither renderer reproduces seasonal shader grading,
procedural game wind, runtime reaction blending, or collision physics. Skin/bone
assignments are retained, so reactions remain available, but the redistributed
foliage still needs a visual animation review and eventual in-game validation.

Run geometry checks:

```powershell
private/venv/Scripts/python.exe -m unittest discover -s tests -p round_oak_test.py
```

## Fruit attachments

The catalog's Fruit selector shows the six original fruit meshes at the game's
model-local attachment origins on mature trees. Stock and revised panes use their
respective origins. Fruit is hidden during skeletal clips: the preview does not
simulate the game's separate attached-item motion.

`tools/tree_fruit_layout.py` writes `tree_fruit_offsets.txt` alongside the generated
forest. Install that file at `private/game/mods/freecamera/tree_fruit_offsets.txt`
with the matching model archives. Normal/blossom oak origins move toward the
revised front foliage. Snow origins follow the exact art-directed branch
transforms. Palm origins remain stock.

The runtime validates the ACNH 3.0.3 instruction fingerprints and the complete
original fruit-origin table before applying the companion profile. The table is
shared by hanging fruit and drop setup; loose fruit meshes are unchanged. The
seasonal profile follows successful opens of the outdoor oak model archive.
This selection assumes the game loads the active outdoor seasonal archive; mixed
seasonal-resource loading and seasonal transitions still require live validation.
Malformed profiles or unmatched game versions retain the original table.

Validation performed: C++ profile parsing and camera tests, all 156 catalog models
and 903 skeletal poses, species/clip visibility checks, and software-rendered
stock/revised oak and snow-oak fruit comparisons. Runtime compilation succeeds;
these checks do not substitute for checking hanging and shaken fruit in game.
