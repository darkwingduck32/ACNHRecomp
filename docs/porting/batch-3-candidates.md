# ACNH 3.0.3 batch 3 candidate queue

The [ledger](ledger.md#active-batch-3) contains the first ten entries. This
queue adds 90 distinct entries on build `FF1D1C05670DB6021C85B624A710B963`.

The scan followed direct calls up to seven levels from 61 gameplay caller
roots. Roots include call sites observed in the disposable shovel probe.
The scanner checked unwind ranges, complete instruction decoding, a return,
no nested calls and no branches outside the function. `Observed` records
a direct caller that appeared in the probe and was checked against the
original BL instruction. `Discovery` records a static caller with a live
trigger still to establish. The scheduler prioritizes observed routes.

The probe is `tree-shake-original-more1/console.log` in the local campaign
directory. Its SHA-256 is
`3482eefe2937866a365f528050f49054ca0dcfb5666c6878486b66f995d93974`.
Recreate the queue from the campaign worktree.

```powershell
& private/venv/Scripts/python -m private.heading.discover_batch3_extra
```

| Main entry | Unwind size | Direct caller | Stage |
| --- | --- | --- | --- |
| `0x21e208` | `0x5c` | `0x226ca0` | Observed |
| `0x3753b94` | `0x68` | `0x19e82c4` | Observed |
| `0x22254c` | `0x100` | `0x221bac` | Observed |
| `0x3da9348` | `0x108` | `0x648c18` | Observed |
| `0x380a8a0` | `0x14c` | `0x380a7c4` | Observed |
| `0x16f3a18` | `0x164` | `0x16bb50c` | Observed |
| `0x380e4c0` | `0x180` | `0x3868734` | Observed |
| `0x862530` | `0x1cc` | `0x867a6c` | Observed |
| `0x19d6a30` | `0xbc` | `0x157a698` | Observed |
| `0x1b65b60` | `0xf4` | `0x30938ac` | Observed |
| `0x247a010` | `0x114` | `0x240c5ac` | Observed |
| `0x16c0910` | `0x120` | `0x852af4` | Observed |
| `0x1217b90` | `0x124` | `0x12d264c` | Observed |
| `0x29cc800` | `0x124` | `0x29cd450` | Observed |
| `0x29cc930` | `0x124` | `0x29cd460` | Observed |
| `0x29cca60` | `0x124` | `0x29cd470` | Observed |
| `0x29ccb90` | `0x124` | `0x29cd490` | Observed |
| `0x19d778c` | `0x140` | `0x24572c0` | Observed |
| `0x1285040` | `0x14c` | `0x87aae4` | Observed |
| `0x19e89e0` | `0x178` | `0x659b6c` | Observed |
| `0x880630` | `0x19c` | `0x87c0bc` | Observed |
| `0x880870` | `0x19c` | `0x87733c` | Observed |
| `0x881090` | `0x1b0` | `0x87c0ec` | Observed |
| `0x1285fe0` | `0x1c4` | `0x87c048` | Observed |
| `0x19d7ca0` | `0x1c8` | `0x160fb50` | Observed |
| `0x19d78cc` | `0x1d8` | `0x2225f58` | Observed |
| `0x328b5cc` | `0x18` | `0x3091a68` | Discovery |
| `0x2375f8` | `0x24` | `0x226e64` | Discovery |
| `0x23a8c8` | `0x24` | `0x226e70` | Discovery |
| `0x21e3f4` | `0x3c` | `0x225d8c` | Discovery |
| `0x21e094` | `0x64` | `0x221d2c` | Discovery |
| `0x245a688` | `0x110` | `0x220fb24` | Discovery |
| `0x867538` | `0x130` | `0x865330` | Discovery |
| `0x867668` | `0x13c` | `0x8658e8` | Discovery |
| `0x866f60` | `0x170` | `0x865ec0` | Discovery |
| `0x8670d0` | `0x178` | `0x8663d0` | Discovery |
| `0x867248` | `0x178` | `0x865340` | Discovery |
| `0x3daf030` | `0x1a8` | `0x3dafc18` | Discovery |
| `0x3b150d0` | `0x210` | `0x2465604` | Discovery |
| `0x2469558` | `0x4c` | `0x2468a4c` | Discovery |
| `0x280cbcc` | `0x6c` | `0x3093844` | Discovery |
| `0x32fa3b0` | `0x6c` | `0x3093c24` | Discovery |
| `0x127d59c` | `0x70` | `0x157c628` | Discovery |
| `0x1595708` | `0x80` | `0x157abfc` | Discovery |
| `0x1596154` | `0x8c` | `0x10de010` | Discovery |
| `0x19d9fd0` | `0xa4` | `0x157a4d0` | Discovery |
| `0x1595840` | `0xb8` | `0x157a3c0` | Discovery |
| `0x19ee2f4` | `0xc0` | `0x157a560` | Discovery |
| `0x19f49bc` | `0xc0` | `0x157a674` | Discovery |
| `0x1a15608` | `0xc8` | `0x157a500` | Discovery |
| `0x1a156d0` | `0xc8` | `0x157abe0` | Discovery |
| `0x1217cc0` | `0xe4` | `0x157c638` | Discovery |
| `0x3c49da4` | `0xf8` | `0x3093ab8` | Discovery |
| `0x194fa00` | `0xfc` | `0x1578d54` | Discovery |
| `0x194f680` | `0x118` | `0x157db4c` | Discovery |
| `0x1284ee0` | `0x15c` | `0x84bbc8` | Discovery |
| `0x16bfae0` | `0x198` | `0x852ae8` | Discovery |
| `0x8ceb5c` | `0x218` | `0x246420c` | Discovery |
| `0x21e264` | `0x78` | `0x227d30` | Discovery |
| `0x21e0f8` | `0x84` | `0x221e70` | Discovery |
| `0x21e17c` | `0x84` | `0x221e90` | Discovery |
| `0x21e980` | `0xc8` | `0x21df6c` | Discovery |
| `0x21e2dc` | `0xdc` | `0x227cf0` | Discovery |
| `0x128367c` | `0xe4` | `0x11e5ab4` | Discovery |
| `0x84b728` | `0x118` | `0x87df00` | Discovery |
| `0x84b840` | `0x118` | `0x15755c0` | Discovery |
| `0x8677a4` | `0x13c` | `0x865eb0` | Discovery |
| `0x8678e0` | `0x13c` | `0x8663c0` | Discovery |
| `0x240b360` | `0x15c` | `0x2482f58` | Discovery |
| `0x22295ec` | `0x16c` | `0x225aef0` | Discovery |
| `0x8673c0` | `0x178` | `0x8658f8` | Discovery |
| `0x2832e20` | `0x128` | `0x15ed758` | Discovery |
| `0x880a10` | `0x19c` | `0x8775d0` | Discovery |
| `0x113b5b0` | `0x214` | `0x1150a44` | Discovery |
| `0x237620` | `0x20` | `0x222b7c` | Discovery |
| `0x23a8f0` | `0x20` | `0x222b64` | Discovery |
| `0x22c410` | `0x2c` | `0x23ddd8` | Discovery |
| `0x21e3b8` | `0x30` | `0x222bb0` | Discovery |
| `0x8b950c` | `0x70` | `0x8b94c4` | Discovery |
| `0x12919f8` | `0x7c` | `0x12938c8` | Discovery |
| `0x874bc8` | `0x150` | `0x87e184` | Discovery |
| `0x120d468` | `0x168` | `0x1209f70` | Discovery |
| `0x7ea6d8` | `0x214` | `0x7eaa98` | Discovery |
| `0x5f1750` | `0x6c` | `0x5f3e54` | Discovery |
| `0x19e893c` | `0xa4` | `0x26a9d78` | Discovery |
| `0x12837d0` | `0xb0` | `0x15e0bb8` | Discovery |
| `0x120d9a0` | `0xcc` | `0x120e7d8` | Discovery |
| `0x120e980` | `0xec` | `0x120e720` | Discovery |
| `0x1298524` | `0x104` | `0x129302c` | Discovery |
| `0x113cc10` | `0x10c` | `0x114c9d8` | Discovery |
