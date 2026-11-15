# 360 Camera feature package

The feature is compiled into the player through `nx_camera`, a source-level
CMake target. Its implementation, game-version adapter and renderer adapter live
in `runtime/src/features/camera/`. This is not a DLL plugin and does not work with
an unmodified upstream player.

A single `freecamera.zip` contains the reviewed trees, cloud/ocean assets, resource
size table, fruit offsets, fog shader profile and camera parameter edits. Import
it through the updated launcher's Mods manager and restart. Installation preserves
the complete package and uses the manifest ID for upgrades instead of creating
`freecamera-2`. Existing `camera303` enablement is migrated; its files are retained.
The optional extended tool durability mod remains a separate toggle/package.

SpeedFix and the translation are native release content. They are neither inputs
nor payloads of our package, and cannot be replaced or superseded by a feature
manifest. Their packaging and enablement remain independent.

## Build and reproduce

From the repository root:

```powershell
python tools/build_camera_package.py --game private/game --out private/share/packages
```

`assets.lock.json` pins the reviewed installed input assets by relative path and
SHA-256. The builder refuses changed assets. `--freeze` deliberately replaces this
lock after review; it is not part of normal builds. The asset files remain local,
outside source distributions. No saves, configuration, installation reports,
backups, build paths or file timestamps are included in the generated packages.
ZIP member order, timestamps and permissions are normalized.

For release staging, first run the existing native mod bundler unchanged. Then:

```powershell
python tools/bundle_camera_package.py private/release/staging
```

This adds the two feature packages to the existing `mods/bundle.txt` inventory.
It does not fetch, rewrite or take ownership of the native mods. The generated default preset enables our camera and durability packages on their
first installation, migrating the legacy camera entry while preserving native
settings. A saved per-feature marker prevents updates from re-enabling a mod the
user subsequently disabled. An explicit `NX_MODS` launch override takes priority.

## Parameter compatibility

The package does not ship `StaticParam.pack`. Its camera-only scalar edits are
applied to the archive already selected by LayeredFS, in a temporary host file.
The original file stays untouched. Each edit checks the expected old value (or
its already-patched value), and the archive header/layout must match. Unrelated
native mod edits are preserved. A conflict rejects the entire adaptation without
partial edits and disables camera runtime input/activation for that session.

The importer rejects unsupported build IDs, missing/extra files, checksum
mismatches, unsafe ZIP paths and unsupported runtime capabilities. Legacy RomFS
imports retain their previous behavior. Manifest hashes establish integrity and
reproducibility, not publisher authentication.

## Rebase onto a newer player

1. Carry `nx_camera`, the package importer/validator and the small calls from input,
   UI gating, module loading, file access, temporal camera capture and renderer
   uniform/background-depth handling.
2. Keep general renderer performance work, timing and Saves-menu fixes separate
   from feature changes. They are still independent changes in this working tree;
   no commits or branches have been rewritten for this refactor.
3. Run package, camera, background-scan and hook-profile tests. Recheck renderer
   upload allocation and epoch/cache semantics if upstream changed them.
4. Rebuild the player. Rebuild the imported guest library when the recompiler ABI
   or required hook profile changed.

## Port onto a newer game build

This requires actual compatibility work, not just changing a version label.
`version_303.h` holds the current addresses, instruction expectations and shader
identities. Structure field offsets remain with the adapter code that uses them.
Port and validate those layouts too. The feature hook list is included from the
base game hook profile; included-profile changes invalidate generated code.

Revalidate parameter field identities/layout, model resource budgets, fruit
origins, shader profiles and the input/UI/interior gates against the new build.
Then create its adapter, add the supported build ID and freeze reviewed assets.
Unknown builds are rejected. No automatic support for future game versions is
claimed.

## Validation

```powershell
ctest --test-dir private/acnh/build -R "mod_package|freecamera|background_scan" --output-on-failure
python -m unittest tests.hook_profile_test
```

`mod_package_test` covers checksums, path safety, stable-ID upgrades, folder and ZIP
profile preservation, failed-update preservation, native-mod coexistence, and
atomic parameter conflict refusal. It also accepts a real camera ZIP, native
parameter archive and stock parameter archive as three arguments to verify that
every native-modified byte survives camera adaptation.

The refactor has been compiled on Windows and tested offline. Live gameplay,
other platforms and every weather/interior combination still need validation
before treating this as a release candidate.
