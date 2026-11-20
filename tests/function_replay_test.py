import unittest

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1

from tests import function_replay


class FunctionReplayTest(unittest.TestCase):
    def test_returns_and_preserves_independent_input(self):
        # ldr w1, [x0]; add w1, w1, #1; str w1, [x0]; ret
        code = bytes.fromhex("010040b921040011010000b9c0035fd6")
        data = (41).to_bytes(4, "little")
        registers = {UC_ARM64_REG_X0: 0x20000}
        first = function_replay.run(0x10000, code, registers, (UC_ARM64_REG_X1,), {0x20000: data})
        second = function_replay.run(0x10000, code, registers, (UC_ARM64_REG_X1,), {0x20000: data})
        self.assertEqual(first, second)
        self.assertEqual(first.registers, {UC_ARM64_REG_X1: 42})
        self.assertEqual(first.memory, {0x20000: (42).to_bytes(4, "little")})
        self.assertEqual(first.written_bytes, frozenset(range(0x20000, 0x20004)))

    def test_preserves_transient_write_order(self):
        # mov w1, #1; str w1, [x0]; str wzr, [x0]; ret
        code = bytes.fromhex("21008052010000b91f0000b9c0035fd6")
        result = function_replay.run(0x10000, code, {UC_ARM64_REG_X0: 0x20000}, (), {0x20000: bytes(4)})
        self.assertEqual(result.memory, {0x20000: bytes(4)})
        self.assertEqual(result.writes, ((0x20000, 4, 1), (0x20000, 4, 0)))

    def test_rejects_read_beyond_fixture_in_mapped_page(self):
        # ldr w1, [x0, #4]; ret
        code = bytes.fromhex("010440b9c0035fd6")
        with self.assertRaisesRegex(ValueError, "unexpected read"):
            function_replay.run(0x10000, code, {UC_ARM64_REG_X0: 0x20000}, (), {0x20000: bytes(4)})

    def test_rejects_execution_outside_function(self):
        # bl +8; ret
        code = bytes.fromhex("02000094c0035fd6")
        with self.assertRaisesRegex(ValueError, "unexpected call"):
            function_replay.run(0x10000, code, {}, (), {})

    def test_replays_a_declared_direct_callee(self):
        # stp x29, x30, [sp, #-16]!; bl 0x10100; ldp x29, x30, [sp], #16; ret
        code = bytes.fromhex("fd7bbfa93f000094fd7bc1a8c0035fd6")
        callee = bytes.fromhex("001c0091c0035fd6")  # add x0, x0, #7; ret
        result = function_replay.run(0x10000, code, {UC_ARM64_REG_X0: 35},
                                     (UC_ARM64_REG_X0,), {}, callees={0x10004: (0x10100, callee)})
        self.assertEqual(result.registers[UC_ARM64_REG_X0], 42)
        self.assertEqual(result.calls, ((0x10004, 0x10100, 35),))

    def test_replays_a_declared_tail_call(self):
        code = bytes.fromhex("40000014")  # b 0x10100
        callee = bytes.fromhex("001c0091c0035fd6")  # add x0, x0, #7; ret
        result = function_replay.run(0x10000, code, {UC_ARM64_REG_X0: 35},
                                     (UC_ARM64_REG_X0,), {}, callees={0x10000: (0x10100, callee)})
        self.assertEqual(result.registers[UC_ARM64_REG_X0], 42)
        self.assertEqual(result.calls, ((0x10000, 0x10100, 35),))

    def test_rejects_call_not_matching_declared_target(self):
        code = bytes.fromhex("fd7bbfa93f000094fd7bc1a8c0035fd6")
        callee = bytes.fromhex("c0035fd6")
        with self.assertRaisesRegex(ValueError, "unexpected call"):
            function_replay.run(0x10000, code, {}, (), {}, callees={0x10004: (0x10200, callee)})

    def test_rejects_non_return_branch_to_sentinel(self):
        # br x30
        with self.assertRaisesRegex(ValueError, "RET"):
            function_replay.run(0x10000, bytes.fromhex("c0031fd6"), {}, (), {})

    def test_rejects_instruction_budget_exhaustion(self):
        # b .
        with self.assertRaisesRegex(ValueError, "return sentinel"):
            function_replay.run(0x10000, bytes.fromhex("00000014"), {}, (), {}, instruction_limit=3)


if __name__ == "__main__":
    unittest.main()
