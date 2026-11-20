# ACNH 3.0.3 batch 3 candidate queue

This discovery queue tracks 100 entries on build
`FF1D1C05670DB6021C85B624A710B963`. The [ledger](ledger.md#active-batch-3)
tracks candidates selected for implementation.

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
The additional probe at `tree-shake-original-rowcache2/console.log`
verified 65 queued entries against original direct calls. Its SHA-256 is
`087d3fabad7ab88f43a48707bd35c354da08a2f76916125ad6b70019993df474`.
The additional probe at `tree-shake-original-terraintriangle1/console.log`
verified 58 queued entries against original direct calls. Its SHA-256 is
`2873b7d69db36bf814273e2cd41452b5bd818db47d656172a28fa7c0a794a3f1`.
After moving the eight verified triangle predicates to the ledger and
refilling the queue, 54 entries have observed direct callers.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
& private/venv/Scripts/python -m private.heading.discover_batch3_extra `
    --probe "$r/tree-shake-original-rowcache2/console.log" 087d3fabad7ab88f43a48707bd35c354da08a2f76916125ad6b70019993df474 `
    --probe "$r/tree-shake-original-terraintriangle1/console.log" 2873b7d69db36bf814273e2cd41452b5bd818db47d656172a28fa7c0a794a3f1
```

| Main entry | Unwind size | Direct caller | Stage |
| --- | --- | --- | --- |
| `0x21e208` | `0x5c` | `0x226ca0` | Observed |
| `0x3753b94` | `0x68` | `0x19e82c4` | Observed |
| `0x22254c` | `0x100` | `0x221bac` | Observed |
| `0x3da9348` | `0x108` | `0x648c18` | Observed |
| `0x380a8a0` | `0x14c` | `0x380a7c4` | Observed |
| `0x380e4c0` | `0x180` | `0x3868734` | Observed |
| `0x19d6a30` | `0xbc` | `0x157a698` | Observed |
| `0x1b65b60` | `0xf4` | `0x30938ac` | Observed |
| `0x247a010` | `0x114` | `0x240c5ac` | Observed |
| `0x16c0910` | `0x120` | `0x852af4` | Observed |
| `0x1217b90` | `0x124` | `0x12d264c` | Observed |
| `0x19d778c` | `0x140` | `0x24572c0` | Observed |
| `0x19e89e0` | `0x178` | `0x659b6c` | Observed |
| `0x19d7ca0` | `0x1c8` | `0x160fb50` | Observed |
| `0x19d78cc` | `0x1d8` | `0x2225f58` | Observed |
| `0x328b5cc` | `0x18` | `0x3091a68` | Observed |
| `0x21e3f4` | `0x3c` | `0x225d8c` | Observed |
| `0x21e094` | `0x64` | `0x226540` | Observed |
| `0x245a688` | `0x110` | `0x220ff10` | Observed |
| `0x280cbcc` | `0x6c` | `0x1d28308` | Observed |
| `0x127d59c` | `0x70` | `0x157c628` | Observed |
| `0x1596154` | `0x8c` | `0x157e75c` | Observed |
| `0x19d9fd0` | `0xa4` | `0x157ac20` | Observed |
| `0x19f49bc` | `0xc0` | `0x157a674` | Observed |
| `0x1217cc0` | `0xe4` | `0x157c638` | Observed |
| `0x3c49da4` | `0xf8` | `0x330bc7c` | Observed |
| `0x194fa00` | `0xfc` | `0x144c9b8` | Observed |
| `0x16bfae0` | `0x198` | `0x852ae8` | Observed |
| `0x8ceb5c` | `0x218` | `0x246420c` | Observed |
| `0x21e264` | `0x78` | `0x227d30` | Observed |
| `0x21e0f8` | `0x84` | `0x221e70` | Observed |
| `0x21e17c` | `0x84` | `0x221e90` | Observed |
| `0x21e980` | `0xc8` | `0x21df6c` | Observed |
| `0x21e2dc` | `0xdc` | `0x227cf0` | Observed |
| `0x128367c` | `0xe4` | `0x12622a8` | Observed |
| `0x84b728` | `0x118` | `0x15753e0` | Observed |
| `0x84b840` | `0x118` | `0x15755c0` | Observed |
| `0x22295ec` | `0x16c` | `0x225aef0` | Observed |
| `0x113b5b0` | `0x214` | `0x9d57f0` | Observed |
| `0x21e3b8` | `0x30` | `0x222bb0` | Observed |
| `0x874bc8` | `0x150` | `0x87e184` | Observed |
| `0x120d468` | `0x168` | `0x1209f70` | Observed |
| `0x7ea6d8` | `0x214` | `0x7eaa98` | Observed |
| `0x1298524` | `0x104` | `0x129302c` | Observed |
| `0x19e8b58` | `0x118` | `0x142de9c` | Observed |
| `0x22e0f4` | `0x234` | `0x22e79c` | Observed |
| `0x2603058` | `0xdc` | `0x2542d54` | Observed |
| `0x32d4244` | `0x1a4` | `0x32d4214` | Observed |
| `0x3a0d9a0` | `0x1e0` | `0x3c4ce38` | Observed |
| `0x26c0ee4` | `0x44` | `0x25e7f68` | Observed |
| `0x2a673a0` | `0x14c` | `0x2a590f8` | Observed |
| `0x2a67620` | `0x194` | `0x2a59264` | Observed |
| `0x2a72930` | `0x1ec` | `0x2a6b870` | Observed |
| `0x6b3b6c` | `0x150` | `0x6b3da4` | Observed |
| `0x2375f8` | `0x24` | `0x226e64` | Discovery |
| `0x23a8c8` | `0x24` | `0x226e70` | Discovery |
| `0x3daf030` | `0x1a8` | `0x3dafc18` | Discovery |
| `0x3b150d0` | `0x210` | `0x2465604` | Discovery |
| `0x2469558` | `0x4c` | `0x2468a4c` | Discovery |
| `0x32fa3b0` | `0x6c` | `0x3093c24` | Discovery |
| `0x1595708` | `0x80` | `0x157abfc` | Discovery |
| `0x1595840` | `0xb8` | `0x157a3c0` | Discovery |
| `0x19ee2f4` | `0xc0` | `0x157a560` | Discovery |
| `0x1a15608` | `0xc8` | `0x157a500` | Discovery |
| `0x1a156d0` | `0xc8` | `0x157abe0` | Discovery |
| `0x194f680` | `0x118` | `0x157db4c` | Discovery |
| `0x240b360` | `0x15c` | `0x2482f58` | Discovery |
| `0x2832e20` | `0x128` | `0x15ed758` | Discovery |
| `0x237620` | `0x20` | `0x222b7c` | Discovery |
| `0x23a8f0` | `0x20` | `0x222b64` | Discovery |
| `0x22c410` | `0x2c` | `0x23ddd8` | Discovery |
| `0x8b950c` | `0x70` | `0x8b94c4` | Discovery |
| `0x12919f8` | `0x7c` | `0x12938c8` | Discovery |
| `0x5f1750` | `0x6c` | `0x5f3e54` | Discovery |
| `0x19e893c` | `0xa4` | `0x26a9d78` | Discovery |
| `0x12837d0` | `0xb0` | `0x15e0bb8` | Discovery |
| `0x120d9a0` | `0xcc` | `0x120e7d8` | Discovery |
| `0x120e980` | `0xec` | `0x120e720` | Discovery |
| `0x113cc10` | `0x10c` | `0x114c9d8` | Discovery |
| `0x1a160f0` | `0x240` | `0x2690890` | Discovery |
| `0x1a164e0` | `0x240` | `0x2690974` | Discovery |
| `0x377de70` | `0x88` | `0x1cb1f18` | Discovery |
| `0x114b2a0` | `0xdc` | `0x1142f88` | Discovery |
| `0x170e2d0` | `0xdc` | `0x83bef4` | Discovery |
| `0x113c9b0` | `0x10c` | `0x114c54c` | Discovery |
| `0x16c0740` | `0x1c8` | `0x83c0a8` | Discovery |
| `0x2a72fd0` | `0x108` | `0x2a8f2d8` | Discovery |
| `0x16c0620` | `0x120` | `0x85a0f0` | Discovery |
| `0x230cb0` | `0x1d8` | `0x230854` | Discovery |
| `0x230ac0` | `0x1f0` | `0x23063c` | Discovery |
| `0x9bfe70` | `0xfc` | `0x26a391c` | Discovery |
| `0x16c0a30` | `0x120` | `0x16f90c4` | Discovery |
| `0x271836c` | `0x28` | `0x2681878` | Discovery |
| `0x26ebb14` | `0x5c` | `0x2682f20` | Discovery |
| `0x3757320` | `0x88` | `0x1f1db9c` | Discovery |
| `0x281731c` | `0x6c` | `0x1d43134` | Discovery |
| `0x2817388` | `0x6c` | `0x2ded008` | Discovery |
| `0x170bc10` | `0x70` | `0x1f1d47c` | Discovery |
| `0x1cbd970` | `0x78` | `0x1f1c514` | Discovery |
| `0x170b220` | `0x80` | `0x1f1cca4` | Discovery |
