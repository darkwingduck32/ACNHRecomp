# Cosine of the shortest angular separation

Main build `FF1D1C05670DB6021C85B624A710B963`, entry `0x5f06dc`, unwind
range `0x54` bytes. W0 and W1 are unsigned turn units. S0 returns the cosine
of their shortest separation. The GOT relative relocation at
`main+0x4ab2d00` points to the lookup table at `main+0x4099798`. The leaf
writes no guest memory and calls nothing externally.

The independent replay passed 1,367 cases twice. The constant-zero
candidate failed on two zero angles with expected S0 bits `3f800000`.
The Release player passed 13 CTest tests and its incremental DLL rebuilt
both shards with the three generated direct calls.

The original gameplay run through the tree shake and island walk ended
with zero hits. The next retry requires a reproducible trigger for
`main+0x1655a34` or `main+0x1df2a0c`. The original remains the default.
C++ is opt-in through `NX_ACNH_ANGULAR_COSINE=cpp`, with fallback when
the GOT points elsewhere. The live trial log is
`tree-shake-original-cosine1/nx.log` under the campaign directory.

```powershell
clang-cl /nologo /std:c++20 /O2 /fp:precise /Foprivate/heading/angular_cosine.obj /Feprivate/heading/angular_cosine.exe tests/acnh_angular_cosine_candidate.cpp
& private/venv/Scripts/python -m tests.acnh_angular_cosine_replay H:/ACNH/ACNHRecomp/private/game/exefs/main private/heading/angular_cosine.exe
```
