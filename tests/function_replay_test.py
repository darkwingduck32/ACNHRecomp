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
