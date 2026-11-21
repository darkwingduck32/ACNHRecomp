import tempfile
import unittest
from pathlib import Path
from recomp import analysis, emit, nso
from recomp.hooks import read_hooks, hook_sources, hooks_fingerprint
class HookProfileTest(unittest.TestCase):
    def test_nested_profiles_and_cycles(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);(root/'base').write_text('10 # shared\ninclude feature\n')
            (root/'feature').write_text('20\n10\n')
            self.assertEqual(read_hooks(root/'base'),{0x10,0x20})
            self.assertEqual(len(hook_sources(root/'base')),2)
            before=hooks_fingerprint(root/'base')
            (root/'feature').write_text('30\n')
            self.assertNotEqual(before,hooks_fingerprint(root/'base'))
            (root/'feature').write_text('include base\n')
            with self.assertRaises(ValueError):read_hooks(root/'base')
    def test_camera_hooks_present(self):
        root=Path(__file__).resolve().parents[1]
        hooks=read_hooks(root/'games/acnh/hooks-3.0.3.txt')
        self.assertTrue({0x37e4724,0x1462bb8,0x11fe020,0x14b76e8,0x10d660c,0x146ac2c,0x1571a70,0x146b1d4,0x146b1dc}<=hooks)

    def test_resume_hook(self):
        from recomp.hooks import read_resumes

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            profile = root / "main"
            profile.write_text("include feature\n# resume 0 8\n", encoding="ascii")
            (root / "feature").write_text("# resume 0 8 # same resume\n4\n", encoding="ascii")
            resumes = read_resumes(profile)
            self.assertEqual(resumes, {0: 8})
            self.assertEqual(read_hooks(profile), {4})
            for directive in ("# resume 0 4", "# resume 0 9", "# resume 8 0", "# resume 0 0"):
                (root / "feature").write_text(directive, encoding="ascii")
                with self.subTest(directive=directive), self.assertRaises(ValueError):
                    read_resumes(profile)

        # ADD X0, X0, #1; NOP; RET; RET.
        image = bytearray.fromhex("000400911f2003d5c0035fd6c0035fd6")
        module = nso.Module("test", image, (0, len(image)), (0, 0), (0, 0), (0, 0))
        func, next_func = analysis.Function(0, 12), analysis.Function(12, 16)
        program = analysis.Analysis(module, {}, functions=[func, next_func])
        emitter = emit.Emitter(program, resumes=resumes)
        output = emitter.function(func)
        self.assertRegex(output, r"if \(have_resume\) \{.*nx_hook_resume\(c, NXB \+ 0x0ull, resume\);")
        self.assertIn("if (resume) goto L_8;", output)
        self.assertIn("add x0, x0, #1", output)
        self.assertIn("L_8:;", output)
        self.assertNotIn("nx_hook", emitter.function(next_func))
        with self.assertRaisesRegex(ValueError, "same function"):
            emit.Emitter(program, resumes={0: 12}).function(func)

        # An absent optional symbol skips the sync, so a dirty register must still spill at the join.
        after_add = emit.Emitter(program, resumes={4: 8}).function(func)
        self.assertRegex(after_add, r"if \(have_resume\) \{.*c\.x\[0\] = x0;.*nx_hook_resume")
        self.assertRegex(after_add, r"L_8:;\n.*c\.x\[0\] = x0;.*return;")
if __name__=='__main__':unittest.main()
