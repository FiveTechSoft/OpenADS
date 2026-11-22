#!/usr/bin/env python3
"""Host-independent packaging failure gates, with explicitly synthetic tool output."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('static_packager', ROOT / 'tools/scripts/package_static_client.py')
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class PackagingGates(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.runtime = self.root / 'runtime'
        self.link = self.runtime
        for name in ('ar', 'objcopy', 'nm', 'gcc'):
            path = self.runtime / 'bin' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('synthetic tool, never executed')
        self.notices = ('gcc/COPYING3', 'gcc/COPYING.RUNTIME', 'crt/COPYING.MinGW-w64-runtime.txt', 'winpthreads/COPYING')
        for name in self.notices:
            path = self.runtime / 'share/licenses' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('SYNTHETIC TEST NOTICE ' + name)
        for name in ('libstdc++.a', 'libgcc.a', 'libgcc_eh.a', 'libwinpthread.a'):
            (self.runtime / name).write_bytes(b'!<arch>\nsynthetic runtime')
        self.engine = self.root / 'engine.a'
        self.engine.write_bytes(b'!<arch>\nsynthetic engine')
        self.glue = self.root / 'glue.c'
        self.glue.write_text('synthetic wrapper')
        self.output = self.root / 'kit/libopenace32.a'
        self.target = 'i686-w64-mingw32'
        self.missing_exports = False
        self.empty_exports = False
        self.missing_runtime = False
        self.target_mismatch = False

    def fake_run(self, *args, cwd=None, text=True, input=None):
        tool, *options = [str(a) for a in args]
        if options == ['-dumpmachine']:
            if self.target_mismatch and Path(tool).parent.parent == self.link:
                return 'x86_64-w64-mingw32\n'
            return self.target + '\n'
        if options == ['-dumpversion']:
            return '15.2.0\n'
        if options and options[0].startswith('-print-file-name='):
            name = options[0].split('=', 1)[1]
            if self.missing_runtime:
                return str(self.root / 'absent.a') + '\n'
            return str(self.runtime / name) + '\n'
        if Path(tool).name == 'ar' and options == ['-M']:
            candidate = Path(input.splitlines()[0].removeprefix('create '))
            candidate.write_bytes(b'!<arch>\nSYNTHETIC COMBINED ARCHIVE')
            return ''
        if Path(tool).name == 'gcc' and '-c' in options:
            Path(options[options.index('-o') + 1]).write_bytes(b'SYNTHETIC OBJECT')
            return ''
        if Path(tool).name == 'nm':
            if options[0] == '-u':
                return '' if self.empty_exports else ' U _AdsProbe\n'
            return '' if self.missing_exports else '0000 T _AdsProbe\n'
        if Path(tool).name == 'ar' and options[0] in ('r', 's'):
            return ''
        raise AssertionError(('unexpected synthetic-tool call', args))

    def execute(self, profile='matching'):
        argv = ['package_static_client.py', '--engine', str(self.engine),
                '--runtime-root', str(self.runtime), '--link-root', str(self.link),
                '--harbour-include', str(self.root), '--glue', str(self.glue),
                '--ace-include', str(self.root), '--output', str(self.output),
                '--profile', profile]
        with patch.object(sys, 'argv', argv), patch.object(packager, 'run', self.fake_run), patch.object(packager.shutil, 'which', return_value=None), patch('builtins.print'):
            packager.main()

    def test_matching_profiles_record_archive_and_all_notice_hashes(self):
        for target in ('i686-w64-mingw32', 'x86_64-w64-mingw32'):
            self.target = target
            self.execute()
            manifest = json.loads(self.output.with_suffix('.manifest.json').read_text())
            self.assertEqual(manifest['runtime_target'], target)
            self.assertEqual(manifest['archive_sha256'], hashlib.sha256(self.output.read_bytes()).hexdigest())
            self.assertEqual(manifest['engine_sha256'], hashlib.sha256(self.engine.read_bytes()).hexdigest())
            self.assertEqual(len(manifest['runtime_notice_sha256']), 4)
            for name, digest in manifest['runtime_notice_sha256'].items():
                self.assertEqual(digest, hashlib.sha256((self.output.parent / 'runtime-notices' / name).read_bytes()).hexdigest())

    def test_missing_notice_stops_packaging(self):
        (self.runtime / 'share/licenses' / self.notices[0]).unlink()
        with self.assertRaisesRegex(SystemExit, 'Missing exact runtime notice'):
            self.execute()
        self.assertFalse(self.output.exists())

    def test_missing_runtime_stops_packaging(self):
        self.missing_runtime = True
        with self.assertRaisesRegex(SystemExit, 'Missing static runtime'):
            self.execute()
        self.assertFalse(self.output.exists())

    def test_missing_wrapper_export_keeps_existing_archive(self):
        self.output.parent.mkdir()
        self.output.write_bytes(b'previous archive')
        self.missing_exports = True
        with self.assertRaisesRegex(SystemExit, 'Missing implementation exports'):
            self.execute()
        self.assertEqual(self.output.read_bytes(), b'previous archive')

    def test_no_wrapper_requirements_is_not_success(self):
        self.empty_exports = True
        with self.assertRaisesRegex(SystemExit, 'Missing implementation exports'):
            self.execute()

    def test_matching_profile_rejects_different_compiler_roots(self):
        self.link = self.root / 'other'
        (self.link / 'bin').mkdir(parents=True)
        (self.link / 'bin/gcc').write_text('synthetic tool')
        with self.assertRaisesRegex(SystemExit, 'same runtime and link compiler'):
            self.execute()

    def test_architecture_mismatch_stops_before_output(self):
        self.link = self.root / 'other'
        (self.link / 'bin').mkdir(parents=True)
        (self.link / 'bin/gcc').write_text('synthetic tool')
        self.target_mismatch = True
        with self.assertRaisesRegex(SystemExit, 'architecture mismatch'):
            self.execute()

    def test_windows_drive_paths_convert_for_msys_python(self):
        with patch.object(packager, 'NATIVE_WINDOWS', False), patch.object(packager.shutil, 'which', return_value='cygpath'), patch.object(packager, 'run', return_value='/d/build/engine.a\n') as call:
            self.assertEqual(packager.host_path('D:/build/engine.a').as_posix(), '/d/build/engine.a')
            call.assert_called_once_with('cygpath', '-u', 'D:/build/engine.a')

    def test_mri_stdin_paths_convert_to_native_drive_paths(self):
        with patch.object(packager.shutil, 'which', return_value='cygpath'), patch.object(packager, 'run', return_value='D:/build/kit.a\n') as call:
            self.assertEqual(packager.mri_path('/d/build/kit.a'), 'D:/build/kit.a')
            call.assert_called_once_with('cygpath', '-m', '/d/build/kit.a')

    def test_legacy_profile_rejects_unpinned_compiler(self):
        with self.assertRaisesRegex(SystemExit, 'requires pinned'):
            self.execute('qt492-x86')


if __name__ == '__main__':
    unittest.main(verbosity=2)
