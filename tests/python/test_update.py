import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import unball_update as updater

SHA = 'a' * 40
OLD = 'b' * 40


class UpdateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="unball update d'água ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def archive(self, sha, destination):
        with zipfile.ZipFile(destination, 'w') as archive:
            archive.writestr('UnBall-Manager2D-' + sha + '/CMakeLists.txt', 'project(test)')
            archive.writestr('UnBall-Manager2D-' + sha + '/assets/COPYING-UNBALL', 'UnBall attribution')

    def build(self, source, build, sha, log):
        build.mkdir()
        executable = build / 'UnBall-Manager2D'
        executable.write_text('#!/bin/sh\nexit 0\n')
        executable.chmod(0o700)
        (build / 'extractor/unball_extract').mkdir(parents=True)
        (build / 'extractor/unball_extract/__main__.py').write_text('')
        (build / 'updater').mkdir()
        (build / 'updater/unball_update.py').write_text('')

    def update(self, **options):
        with patch.object(updater, 'latest_revision', return_value=SHA), \
             patch.object(updater, 'download_archive', side_effect=self.archive), \
             patch.object(updater, 'build_release', side_effect=self.build), \
             patch('sys.stdout', new_callable=io.StringIO):
            updater.perform_update(self.root, OLD, **options)

    def test_atomic_install_contains_modules_and_anchor(self):
        self.update()
        pointer = json.loads((self.root / 'current.json').read_text())
        self.assertEqual(pointer, {'revision': SHA, 'base_revision': OLD})
        bundle = self.root / 'versions' / SHA
        self.assertTrue(os.access(bundle / 'UnBall-Manager2D', os.X_OK))
        self.assertTrue((bundle / 'updater/unball_update.py').exists())
        self.assertTrue((bundle / 'COPYING-UNBALL').exists())
        self.assertFalse(list(self.root.glob('.preparing-*')))
        self.update()  # Reuses validated release without replacing a running executable.
        self.assertEqual(json.loads((self.root / 'current.json').read_text())['base_revision'], OLD)

    def test_failed_build_does_not_activate_and_cleans_stage(self):
        pointer = self.root / 'current.json'
        pointer.write_text(json.dumps({'revision': OLD, 'base_revision': OLD}))
        previous = pointer.read_bytes()
        with patch.object(updater, 'latest_revision', return_value=SHA), \
             patch.object(updater, 'download_archive', side_effect=self.archive), \
             patch.object(updater, 'build_release', side_effect=RuntimeError('compiler failure')), \
             patch('sys.stdout', new_callable=io.StringIO):
            with self.assertRaises(RuntimeError):
                updater.perform_update(self.root, OLD)
        self.assertEqual(pointer.read_bytes(), previous)
        self.assertFalse(list(self.root.glob('.preparing-*')))

    def test_up_to_date_and_check_only_never_download(self):
        with patch.object(updater, 'latest_revision', return_value=SHA), \
             patch.object(updater, 'download_archive') as download, \
             patch('sys.stdout', new_callable=io.StringIO) as output:
            updater.perform_update(self.root, SHA)
            self.assertIn('up_to_date', output.getvalue())
            updater.perform_update(self.root, OLD, check_only=True)
            download.assert_not_called()
        self.assertFalse((self.root / 'current.json').exists())

    def test_archive_paths_and_symlinks_are_rejected(self):
        archive = self.root / 'bad.zip'
        for name in ('../escape', '/absolute', 'root/../../escape', 'root\\..\\escape'):
            with zipfile.ZipFile(archive, 'w') as package:
                package.writestr(name, 'bad')
            with self.assertRaises(ValueError):
                updater.unpack(archive, self.root / 'source')
        with zipfile.ZipFile(archive, 'w') as package:
            link = zipfile.ZipInfo('root/link')
            link.external_attr = 0o120777 << 16
            package.writestr(link, '/outside')
        with self.assertRaises(ValueError):
            updater.unpack(archive, self.root / 'source')

    def test_concurrent_updater_is_rejected(self):
        import fcntl
        with (self.root / '.update.lock').open('a') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            with self.assertRaisesRegex(ValueError, 'Outra janela'):
                updater.perform_update(self.root, OLD)

    def test_incomplete_cached_bundle_does_not_replace_pointer(self):
        self.update()
        (self.root / 'versions' / SHA / 'updater/unball_update.py').unlink()
        pointer = self.root / 'current.json'
        pointer.write_text(json.dumps({'revision': OLD, 'base_revision': OLD}))
        previous = pointer.read_bytes()
        with self.assertRaisesRegex(ValueError, 'incompleta'):
            self.update()
        self.assertEqual(pointer.read_bytes(), previous)

    def test_revision_validation(self):
        for value in ('main', '../other', 'a' * 39, 'g' * 40):
            with self.assertRaises(ValueError):
                updater.revision(value)


if __name__ == '__main__':
    unittest.main()
