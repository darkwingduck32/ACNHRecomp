"""Harness regressions: python -m unittest discover -s tools/play -p test_live.py."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import live


class LiveTests(unittest.TestCase):
    def test_process_probe_does_not_terminate_child(self):
        with tempfile.TemporaryDirectory() as directory:
            child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"])
            try:
                Path(directory, "game.pid").write_text(str(child.pid))
                for _ in range(3):
                    self.assertTrue(live.alive(directory))
                    self.assertIsNone(child.poll())
                child.terminate()
                child.wait(timeout=10)
                self.assertFalse(live.alive(directory))
            finally:
                if child.poll() is None:
                    child.terminate()
                    child.wait(timeout=10)

    def test_repeated_publish_and_native_path(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(live.root_of(directory), str(Path(directory).resolve()))
            path = str(Path(directory, "live_frame"))
            for number in (15, 30, 45):
                live.write_atomic(path, str(number))
                self.assertEqual(live.frame(directory), number)


if __name__ == "__main__":
    unittest.main()
