import tempfile
import unittest
from pathlib import Path

from recomp import analysis, emit, hooks, nso


class DispatchProfileTest(unittest.TestCase):
    def test_opt_in_routes_direct_call_tail_and_fallthrough(self):
        # BL 0xc; B 0xc; NOP; RET
        image = bytearray.fromhex("03000094020000141f2003d5c0035fd6")
        module = nso.Module("test", image, (0, len(image)), (0, 0), (0, 0), (0, 0))
        caller = analysis.Function(0, 8)
        fallthrough = analysis.Function(8, 12)
        target = analysis.Function(12, 16)
        program = analysis.Analysis(module, {}, functions=[caller, fallthrough, target])

        with tempfile.TemporaryDirectory() as directory:
            profile = Path(directory) / "hooks.txt"
            profile.write_text("# dispatch c\n8 # regular hook\n", encoding="ascii")
            self.assertEqual(hooks.read_hooks(profile), {8})
            emitter = emit.Emitter(program, dispatch=hooks.read_dispatch(profile))

        calls = emitter.function(caller)
        self.assertIn("x30 = NXB + 0x4ull;", calls)
        self.assertIn("c.x[30] = x30; nx::call(c, NXB + 0xcull);", calls)
        self.assertRegex(calls, r"nx::call\(c, NXB \+ 0xcull\);.*return;.* // b #0xc")
        self.assertIn("nx::call(c, NXB + 0xcull);", emitter.function(fallthrough))

        original = emit.Emitter(program).function(caller)
        self.assertIn("c.x[30] = x30; sub_0000000C(c);", original)
        self.assertRegex(original, r"sub_0000000C\(c\);.*return;.* // b #0xc")


if __name__ == "__main__":
    unittest.main()
