#!/usr/bin/env python3
"""Exercise production clipboard and undo code in isolated temporary directories."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

DRIVER = Path(os.environ.get('CUPIDFM_COMMAND_DRIVER', str(Path(__file__).resolve().parent / 'file_command_driver')))

class FileCommands(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='cupidfm-commands-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        self.env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ['PATH'])

    def fake(self, command, body):
        path = self.bin / command
        path.write_text('#!/bin/sh\n' + body + '\n')
        path.chmod(0o700)

    def run_command(self, *args):
        return subprocess.run([str(DRIVER), *map(str, args)], cwd=self.root,
                              env=self.env, capture_output=True, text=True)

    def test_clipboard_filename_is_literal(self):
        self.fake('xclip', 'cat > captured')
        source = self.root / 'literal$(touch injected)'
        source.write_text('clipboard data')
        result = self.run_command('clipboard', source)
        self.assertFalse((self.root / 'injected').exists(), result.stderr)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / 'captured').read_text(), 'clipboard data')

    def test_clipboard_failure_is_reported(self):
        self.fake('xclip', 'exit 42')
        source = self.root / 'source'
        source.write_text('data')
        self.assertNotEqual(self.run_command('clipboard', source).returncode, 0)

    def test_copy_filename_is_literal(self):
        source = self.root / 'literal$(touch injected)'
        source.write_text('original')
        dest = self.root / 'destination'
        result = self.run_command('copy', source, dest)
        self.assertFalse((self.root / 'injected').exists(), result.stderr)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(dest.read_text(), 'original')

    def test_copy_failure_is_reported(self):
        self.fake('cp', 'exit 42')
        source = self.root / 'source'
        source.write_text('data')
        self.assertNotEqual(self.run_command('copy', source, self.root/'dest').returncode, 0)

    def test_missing_move_is_reported(self):
        self.assertNotEqual(self.run_command('move', 'missing', 'dest').returncode, 0)

    def test_remove_filename_is_literal(self):
        directory = self.root / 'literal$(touch injected)'
        directory.mkdir()
        result = self.run_command('remove', directory)
        self.assertFalse((self.root / 'injected').exists(), result.stderr)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(directory.exists())

    def test_remove_failure_is_reported(self):
        self.fake('rm', 'exit 42')
        directory = self.root / 'directory'
        directory.mkdir()
        self.assertNotEqual(self.run_command('remove', directory).returncode, 0)

    def test_leading_dash_operand(self):
        (self.root / '-source').write_text('data')
        result = self.run_command('copy', '-source', 'dest')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.root / 'dest').exists())

    def test_clipboard_signal_is_failure(self):
        self.fake('xclip', 'kill -TERM $$')
        source = self.root / 'source'
        source.write_text('data')
        self.assertNotEqual(self.run_command('clipboard', source).returncode, 0)

    def test_paste_literal_name_and_success_log(self):
        source = self.root / 'literal$(touch injected)'
        source.write_text('copied data')
        dest = self.root / 'destination'
        dest.mkdir()
        clipboard = self.root / 'clipboard'
        clipboard.write_text(f'CUPIDFM_CLIP_V2\nOP=COPY\nN=1\n0\t{source}\t{source.name}\n')
        self.fake('xclip', 'cat clipboard')
        result = self.run_command('paste', dest)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), '1 1')
        self.assertFalse((self.root / 'injected').exists())
        self.assertEqual((dest / source.name).read_text(), 'copied data')

    def test_paste_failed_copy_has_no_success_log(self):
        source = self.root / 'source'
        source.write_text('data')
        dest = self.root / 'destination'
        dest.mkdir()
        (self.root / 'clipboard').write_text(f'CUPIDFM_CLIP_V2\nOP=COPY\nN=1\n0\t{source}\tsource\n')
        self.fake('xclip', 'cat clipboard')
        self.fake('cp', 'exit 42')
        result = self.run_command('paste', dest)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout.strip(), '0 0')

    def test_paste_clipboard_failure(self):
        self.fake('xclip', 'exit 42')
        result = self.run_command('paste', self.root)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout.strip(), '-1 0')

if __name__ == '__main__':
    unittest.main()
