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

    def test_move_preserves_dangling_destination_symlink(self):
        source = self.root / 'source'
        source.write_text('data')
        dest = self.root / 'destination'
        dest.symlink_to('missing-target')
        result = self.run_command('move', source, dest)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(dest.is_symlink())
        self.assertEqual(os.readlink(dest), 'missing-target')
        self.assertEqual(source.read_text(), 'data')

    def test_cut_paste_preserves_dangling_destination_symlink(self):
        self.fake('xclip', 'if [ "$3" = "-i" ]; then cat > clipboard; else cat clipboard; fi')
        source = self.root / 'source'
        source.write_text('data')
        dest = self.root / 'destination'
        dest.mkdir()
        (dest / 'source').symlink_to('missing-target')
        self.assertEqual(self.run_command('cut-clipboard', source).returncode, 0)
        result = self.run_command('paste', dest)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((dest / 'source').is_symlink())
        self.assertEqual((dest / 'source (1)').read_text(), 'data')
        self.assertFalse(source.exists())

    def test_copy_symlink_preserves_dangling_destination(self):
        source = self.root / 'source'
        source.symlink_to('source-target')
        dest = self.root / 'destination'
        dest.symlink_to('destination-target')
        result = self.run_command('copy', source, dest)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(os.readlink(dest), 'destination-target')
        self.assertEqual(os.readlink(source), 'source-target')

    def test_paste_respects_existing_numbered_symlinks(self):
        self.fake('xclip', 'if [ "$3" = "-i" ]; then cat > clipboard; else cat clipboard; fi')
        source = self.root / 'source.txt'
        source.write_text('data')
        dest = self.root / 'destination'
        dest.mkdir()
        for name in ['source.txt', 'source (1).txt']:
            (dest / name).symlink_to('missing-target')
        self.assertEqual(self.run_command('copy-clipboard', source).returncode, 0)
        result = self.run_command('paste', dest)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((dest / 'source (2).txt').read_text(), 'data')
        self.assertTrue((dest / 'source.txt').is_symlink())
        self.assertTrue((dest / 'source (1).txt').is_symlink())

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

    def test_cut_then_quit_keeps_source(self):
        self.fake('xclip', 'cat > captured')
        source = self.root / 'keep-me'
        source.write_text('valuable data')
        result = self.run_command('cut-cleanup', source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(source.exists(), 'cut followed by cleanup removed the source')
        self.assertEqual(source.read_text(), 'valuable data')

    def test_v2_paste_name_cannot_escape_destination(self):
        self.fake('xclip', 'cat clipboard')
        source = self.root / 'source'
        source.write_text('data')
        dest = self.root / 'destination'
        dest.mkdir()
        (self.root/'clipboard').write_text(f'CUPIDFM_CLIP_V2\nOP=COPY\nN=1\n0\t{source}\t../escaped\n')
        result = self.run_command('paste', dest)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root/'escaped').exists())

    def clipboard_round_trip(self, cut):
        self.fake('xclip', 'if [ "$3" = "-i" ]; then cat > clipboard; else cat clipboard; fi')
        names = ['normal', 'tab\tname', 'line\nbreak', 'quote"$`name', '-option']
        paths = [self.root / name for name in names]
        for index, path in enumerate(paths):
            path.write_text(f'data {index}')
        dest = self.root / 'destination'
        dest.mkdir()
        write = self.run_command('cut-clipboard' if cut else 'copy-clipboard', *paths)
        self.assertEqual(write.returncode, 0, write.stderr)
        self.assertTrue(all(path.exists() for path in paths))
        result = self.run_command('paste', dest)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), '5 5')
        for index, path in enumerate(paths):
            self.assertEqual((dest / path.name).read_text(), f'data {index}')
            self.assertEqual(path.exists(), not cut)

    def test_v3_copy_round_trip(self):
        self.clipboard_round_trip(False)

    def test_v3_cut_round_trip(self):
        self.clipboard_round_trip(True)

    def test_cut_clipboard_failure_keeps_source(self):
        self.fake('xclip', 'exit 42')
        source = self.root / 'source'
        source.write_text('data')
        self.assertNotEqual(self.run_command('cut-clipboard', source).returncode, 0)
        self.assertEqual(source.read_text(), 'data')

    def test_clipboard_missing_source_does_not_publish(self):
        self.fake('xclip', 'cat > clipboard')
        self.assertNotEqual(self.run_command('copy-clipboard', self.root/'missing').returncode, 0)
        self.assertFalse((self.root/'clipboard').exists())

    def test_malformed_payloads_make_no_changes(self):
        self.fake('xclip', 'cat clipboard')
        source = self.root / 'source'
        source.write_text('data')
        dest = self.root / 'destination'
        dest.mkdir()
        entry = f'0\t{source}\tsource\n'
        raw = str(source).encode()
        payloads = [
            f'CUPIDFM_CLIP_V2\nOP=COPY\nN=2\n{entry}',
            f'CUPIDFM_CLIP_V2\nOP=UNKNOWN\nN=1\n{entry}',
            f'CUPIDFM_CLIP_V2\nOP=COPY\nN=1junk\n{entry}',
            f'CUPIDFM_CLIP_V2\nOP=COPY\nN=1\n{entry}extra',
            f'CUPIDFM_CLIP_V2\nOP=COPY\nN=9999999999999999999999\n{entry}',
            b'CUPIDFM_CLIP_V3\nOP=COPY\nN=1\nP=99999999999999999999999\n',
            b'CUPIDFM_CLIP_V3\nOP=COPY\nN=1\nP=3\na\x00b\n',
            b'CUPIDFM_CLIP_V3\nOP=COPY\nN=1\nP=3\nab',
            f'{source}\n0\nCUT',
            b'CUPIDFM_CLIP_V3\nOP=COPY\nN=2\nP=' + str(len(raw)).encode() + b'\n' + raw + b'\n',
        ]
        for payload in payloads:
            with self.subTest(payload=payload):
                (self.root/'clipboard').write_bytes(payload.encode() if isinstance(payload, str) else payload)
                self.assertNotEqual(self.run_command('paste', dest).returncode, 0)
                self.assertEqual(list(dest.iterdir()), [])
                self.assertEqual(source.read_text(), 'data')

    def test_legacy_copy_still_works(self):
        self.fake('xclip', 'cat clipboard')
        source = self.root / 'source'
        source.write_text('data')
        dest = self.root / 'destination'
        dest.mkdir()
        (self.root/'clipboard').write_text(f'{source}\n0')
        result = self.run_command('paste', dest)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((dest/'source').read_text(), 'data')

    def test_cut_paste_undo_restores_original_location(self):
        self.fake('xclip', 'if [ "$3" = "-i" ]; then cat > clipboard; else cat clipboard; fi')
        source = self.root / 'source'
        source.write_text('data')
        dest = self.root / 'destination'
        dest.mkdir()
        self.assertEqual(self.run_command('cut-clipboard', source).returncode, 0)
        result = self.run_command('paste-undo', dest)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(source.read_text(), 'data')
        self.assertEqual(list(dest.iterdir()), [])

if __name__ == '__main__':
    unittest.main()
