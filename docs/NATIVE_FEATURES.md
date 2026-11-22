# Native feature extensions

This is an internal, source-level extension surface. It is deliberately small,
versioned with the player source, and is not a stable binary ABI or dynamic plugin
loader. Game-specific features belong under `runtime/src/features/<feature>/`;
general byte edits and hashing belong under `runtime/src/common/`. Renderer
integration belongs in `runtime/src/nvn/nvn_extensions.*`.

## Registration and dispatch

Add a `features::Definition` from `register_builtins()` before the registry freezes.
Use a unique lowercase dotted ID, such as `example.weather`. Smaller priorities
run first; ties sort by ID. Registration closes before game or replay threads
start. Add another registration here rather than adding another game-specific
branch to the filesystem, input or renderer.

Callbacks cover initial configuration, loaded modules, pad input, game UI input,
camera updates, projection/depth classification and read-only resource loading.
Absent callbacks have no effect. Input callbacks see earlier callbacks' output.
Classification queries use an OR and can short-circuit; they must have no side
effects. Configuration runs once before game loading. Declare settings needing a
restart through `restart_required`; each feature owns its reload boundary.

Dispatch is synchronous on the caller's thread. Resource and rendering callbacks
may run concurrently; protect shared state or use thread-local state. Borrowed
paths, byte views and event spans expire when the callback returns. Callbacks
must not throw, register features, or retain borrowed memory. Own any retained
state explicitly. IDs, ordering, and callback behavior are source contracts that
can change alongside their callers.

Resource callbacks currently execute while the filesystem open-table lock is
held. They must not reenter guest filesystem operations or wait for work needing
that lock; use their supplied bytes and preloaded native data. Rendering callbacks
must stay on the replay thread and must not issue guest filesystem operations.

## Cooperative resource transforms

External LayeredFS resolution runs first. Native transforms receive the selected
read-only RomFS resource, including files selected from another mod or a packed
archive. Save data, writes, and DLC mounts do not enter this pipeline.

Register `wants_resource` and `patch_resource` together. Interest checks must be
cheap. The transform receives immutable bytes and returns an optional owned
replacement plus a diagnostic. No replacement means no change. Each accepted
output becomes the next feature's input. Returning a diagnostic without output
rejects only that transform and retains earlier accepted edits. Inputs and
outputs are bounded to 128 MiB. Source files remain unchanged; the bridge serves
accepted output from a temporary host file.

`checked_replacement` is for whole reviewed assets: it accepts the known stock
hash, accepts already-replaced data without copying, and skips any other input.
Use a format-aware transform with checked fields when independent edits should
compose. Do not silently overwrite arbitrary external data. `resource_prepared`
observes the final bytes after every transform and successful output preparation;
use it to validate dependencies rather than assuming your output stayed final.

```cpp
features::Definition feature;
feature.id = "example.archive-edit";
feature.wants_resource = [](std::string_view path) {
    return path == "Pack/Example.pack";
};
feature.patch_resource = [](const features::ResourceRequest& request) {
    std::string copy(request.bytes), error;
    if (!edit_checked_fields(copy, error))
        return features::ResourcePatch{{}, error};
    return features::ResourcePatch{std::move(copy), {}};
};
registry.add(std::move(feature));
```

## Native module hooks

`features/module_hooks.h` validates instruction words at module-relative offsets
and installs dispatch wrappers while retaining the previous function in `next`.
Call that function exactly once unless deliberately changing the function's
contract. Never recover an assumed original from a different table: the current
entry may already be another feature's wrapper. Fingerprint checks and bounds
checks precede installation. The camera's terrain wrappers exercise this chain.

Dispatch wrappers do not intercept direct generated calls. Those still require
the recompiler's instruction hooks (`NX_HOOK`) at the appropriate addresses.
Those hooks retain the existing instruction-hook mechanism; no new public hook
ordering or stable ABI is promised here.

## Renderer extensions

Register a named `UniformExtension` before `freeze_uniform_extensions()`. Matching
callbacks return bounded `BytePatch` edits against a host-owned input copy. Each
transform sees accepted earlier edits. Nonconflicting changes compose; conflicting
overlaps reject the entire later patch. Set `ordered_overwrite` explicitly only
when intentional later ownership is part of the feature contract. Invalid edits
never partially apply. GPU-written buffers bypass host rewriting.

The shared renderer allocates and uploads the final copy once. Caching is disabled
unless every matching extension opts in. Cacheable transforms must depend only
on the keyed source/shader/bank/upload epoch/config generation and extension
revision. Call `invalidate_uniform_extensions()` when other feature state changes.
Transforms dependent on other bound buffers or changing camera state should stay
uncached. `host_owned` declares ownership needed by replay routing and must be
consistent with matching and replay routing for the current feature state.

## Build inputs and compatibility

`tools/embed_resources.py` is game-independent: it verifies a checked manifest and
generates private C++ payload units with logical paths and hashes. Absolute input
paths never enter the generated catalog. `tools/build_native_features.py` supplies
the ACNH inventory and enforces both reviewed asset locks. These are build inputs,
not runtime mod installations. Generated payloads are excluded from source ZIPs.

The normal CMake build embeds the checked distributable delta package from
`games/acnh/features/deltas/`. It needs no complete model inputs or stock RomFS
to compile the player. Runtime reconstruction still requires matching imported
game data. A build deliberately omitting the delta package can retain camera
controls and profiles with the optional asset settings unavailable. The launcher
links capability flags rather than the asset payload. Other games register no
ACNH features.

Tests cover deterministic dispatch, cooperative resource composition, rejection,
checked replacements, immutable source input, atomic byte conflicts, wrapper
chaining, instruction bounds with a nonzero text offset, legacy migration, and
checked/repeatable generator output. Actual camera rendering also needs isolated
game harness runs and visual review.

## Native resource deltas and cache

`common/resource_delta.*` decodes a bounded, versioned `NXDP0001` instruction
stream and handles Zstandard frames, including stock frames without content-size
headers. It is independent of game paths, settings and the feature registry.
`common/artifact_cache.*` provides validated, content-addressed artifacts with
same-directory temporary writes and atomic publication. The caller owns the
validation and reconstruction functions. A failed cache write retains verified
in-memory output. Builders must not recursively enter the same cache.

Embedded resource records declare Literal, Delta, or ZstdDelta. Literal is for
source profiles. Model resources must use ZstdDelta: publisher-side generation
decompresses stock and reviewed model archives before creating a patch. Other
binary feature resources use Delta. Complete models are never emitted by the
native data generator; no literal-model fallback is allowed. Python and bsdiff
are build-only dependencies. The player links a pinned Zstandard library and a
small native decoder; it neither launches Python nor downloads model data.

On the first read of an eligible resource during loading, the resolved input
must match the reviewed stock hash. The runtime then validates an existing cache
entry or reconstructs, verifies, recompresses and caches the result. Cache keys
include the source hash, reviewed content hash, encoding, patch hash and codec
policy version. Each entry is checked on reuse. Truncated/corrupt entries are
reconstructed; mismatching third-party source resources are skipped even if a
cache entry exists. Writes never target the imported game or another mod.

Artifacts live at `<data root>/cache/native-resources/v1/`. Only assets actually
requested are generated: unused seasonal/interior resources incur no initial
work. Existing entries survive restarts. Cache contents are disposable local
outputs and must be excluded from distribution, alongside saves and profiles.
For models, final resource observers compare decoded content hashes because
recompression can change the compressed stream while preserving model bytes.
The resource-size table and fruit-layout dependency checks still apply.

The source distribution includes the reviewed delta manifest and patch files;
normal builds need only Python's standard library for embedding. To regenerate
the package from reviewed private stock/replacement inputs, install publisher dependencies with
`python -m pip install -r tools/requirements-native-features.txt`, then configure
CMake with that environment's `Python3_EXECUTABLE` and run
`python tools/build_native_features.py --out private/native-features --export`.
Export enforces the stock/replacement asset locks. Normal builds validate the
distributable package against those locks and verify every patch checksum. Unit tests cover malformed
streams, bounded output, source conflicts, cache corruption/repair, invalidation,
and unwritable-cache fallback. When private inputs exist, a native catalog test
reconstructs and verifies every resource without launching the game.
