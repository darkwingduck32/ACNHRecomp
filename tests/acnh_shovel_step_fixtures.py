"""Synthetic distance, heading and collision paths for the shovel step check."""
import itertools
import struct

BASE = 0x4000000000
SP = 0x6500001000
REQUEST, OUT, PARAMETERS, VTABLE = 0x20000, 0x21000, 0x22000, 0x23000
ENTRY = 0x15f0c64
FRAME = SP - 0xd0


def fixture(capture_type, origin, desired, stretch, direction, heading, plan, have_output):
    requires_collision = plan != "fallback"
    options = 0x3d44012270000000 | (int(stretch) << 16) | (int(direction) << 8) | int(requires_collision)
    output = OUT if have_output else 0
    cap = capture_type(ENTRY, BASE, 0, SP, (REQUEST, output, 0, 0, 0, 0))
    cap.operands = {0: (REQUEST, struct.pack("<6fQ", *origin, *desired, options))}
    if output:
        cap.operands[1] = (OUT, struct.pack("<3I", 0xdeadbeef, 0xa5a5a5a5, 0xcafe9999))

    def call(site, target, *args, result=0, output_bytes=None):
        cap.calls.append((site, target, *args, *([0] * (6-len(args)))))
        cap.outcomes[site] = (int(result), 0)
        if output_bytes:
            cap.effects[site] = output_bytes

    if not stretch and direction:
        call(0x15f0d14, 0x5f0400, result=heading)
    detailed_pass = plan not in ("fallback", "endpoint-blocked")
    success = False
    if requires_collision:
        cfg = PARAMETERS + 0x2a0
        cap.loads = [(cfg, 8, VTABLE), (VTABLE + 0xf0, 8, BASE + 0x60cba4),
                     (cfg + 0x24, 4, struct.unpack("<I", struct.pack("<f", 4.25))[0])]
        call(0x15f0ebc, 0x241e98c, 1, result=PARAMETERS)
        call(0x15f0ed0, 0x60cba4, cfg, result=cfg + 0x24)
        call(0x15f0f10, 0x15ed290, FRAME + 0x34, result=plan == "outer-blocked")
        call(0x15f0f48, 0x15ed290, FRAME + 0x18, result=plan == "endpoint-blocked")
    if detailed_pass:
        for index, (cast, check) in enumerate(((0x15f0fc4, 0x15f0fe0), (0x15f1024, 0x15f1040), (0x15f1088, 0x15f10a4)), 1):
            blocked = plan == "all-casts" or (plan == "second-accept" and index == 1)
            call(cast, 0x15ed290, FRAME + 0x34, result=blocked)
            if blocked:
                continue
            success = ((plan in ("outer-blocked", "first-accept") and index == 1)
                       or (plan == "second-accept" and index == 2)
                       or (plan == "third-accept" and index == 3))
            call(check, 0x15e9c80, FRAME + 4, 1, 1, 0, result=success)
            if success:
                break
    if not success:
        new_output = (OUT, struct.pack("<3f", 30., 20., 40.)) if output and plan == "fallback" else None
        accepted = plan not in ("fallback", "all-casts")
        call(0x15f10bc, 0x15f10f0, FRAME + 0x50, output, result=accepted, output_bytes=new_output)
        success = accepted
        if new_output:
            cap.exit[1] = new_output
    cap.returned = (int(success), 0)
    return cap


def synthetic_cases(capture_type):
    vectors = (
        ((400., 15., 680.), (404.25, 15., 688.75)),
        ((400., -0.0, 680.), (400., -0.0, 680.)),
        ((-19.25, 3., -37.5), (-105.0, -24., -0.125)),
        ((0., 11., 0.), (1.25, 9., -0.625)),
    )
    modes = ((False, False, 0), (True, True, 0x10000000), (False, True, 0xfffffff0),
             (False, True, 0x20000000), (False, True, 0x8fffffff))
    options = ("fallback", "endpoint-blocked", "outer-blocked", "first-accept", "second-accept",
               "third-accept", "all-casts", "all-reject")
    return {f"synthetic-{i}": fixture(capture_type, *pair, *mode, plan, output)
            for i, (pair, mode, plan, output) in enumerate(itertools.product(vectors, modes, options, (True, False)))}
