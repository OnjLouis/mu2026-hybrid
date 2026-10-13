"""Public safety tests using synthetic bytes only; no Yamaha test data."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from bank_common import ConversionError, read_checked, write_bank
from recover_an import valid_scene
from recover_dx import valid_patch


class BankSafetyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def test_checked_input(self):
        source = self.root / 'source'; source.write_bytes(b'fixture')
        sha = hashlib.sha256(b'fixture').hexdigest()
        self.assertEqual(read_checked(source, sha, 7, 7), b'fixture')
        for expected, size, exact in [('0'*64, 7, 7), (sha, 6, None), (sha, 8, 8)]:
            with self.assertRaises(ConversionError):
                read_checked(source, expected, size, exact)

    def test_missing_input(self):
        with self.assertRaises(ConversionError):
            read_checked(self.root / 'absent', '0'*64, 8)

    def test_new_and_identical_output(self):
        output = self.root / 'bank'
        self.assertIn('Created', write_bank(output, b'new'))
        self.assertIn('Already current', write_bank(output, b'new'))
        self.assertEqual(list(self.root.iterdir()), [output])

    def test_no_clobber(self):
        output = self.root / 'bank'; output.write_bytes(b'old')
        with self.assertRaises(ConversionError):
            write_bank(output, b'new')
        self.assertEqual(output.read_bytes(), b'old')

    def test_forced_backup(self):
        output = self.root / 'bank'; output.write_bytes(b'old')
        write_bank(output, b'new', force=True)
        self.assertEqual(output.read_bytes(), b'new')
        self.assertEqual((self.root / 'bank.previous').read_bytes(), b'old')
        write_bank(output, b'newer', force=True)
        self.assertEqual((self.root / 'bank.previous').read_bytes(), b'new')

    def test_input_protection(self):
        output = self.root / 'input'; output.write_bytes(b'old')
        with self.assertRaises(ConversionError):
            write_bank(output, b'new', True, (output,))
        self.assertEqual(output.read_bytes(), b'old')

    def test_stale_lock_preserved(self):
        output = self.root / 'bank'; lock = self.root / 'bank.lock'; lock.write_bytes(b'lock')
        with self.assertRaises(ConversionError):
            write_bank(output, b'new')
        self.assertFalse(output.exists())
        self.assertEqual(lock.read_bytes(), b'lock')

    def test_replace_failure_preserves_output(self):
        output = self.root / 'bank'; output.write_bytes(b'old')
        import os
        real_replace = os.replace
        def replace(source, target):
            if Path(target) == output:
                raise OSError('injected replacement failure')
            return real_replace(source, target)
        with patch('bank_common.os.replace', replace):
            with self.assertRaises(OSError):
                write_bank(output, b'new', True)
        self.assertEqual(output.read_bytes(), b'old')
        self.assertEqual((self.root / 'bank.previous').read_bytes(), b'old')
        self.assertFalse((self.root / 'bank.lock').exists())
        self.assertFalse(list(self.root.glob('.bank-*')))

    def test_parameter_rejection(self):
        self.assertFalse(valid_scene(bytes(121)))
        self.assertFalse(valid_scene(bytes(122)))
        self.assertFalse(valid_patch(bytes(154)))
        self.assertTrue(valid_patch(bytes(155)))
        invalid = bytearray(155); invalid[0] = 100
        self.assertFalse(valid_patch(invalid))

    def test_cli_help_without_pdf_dependencies(self):
        for name in ('recover_an.py', 'recover_dx.py'):
            result = subprocess.run([sys.executable, '-S', str(Path(__file__).with_name(name)), '--help'],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('--force', result.stdout)

    def test_cli_bad_input_preserves_output(self):
        source=self.root/'source'; source.write_bytes(b'not firmware')
        manual=self.root/'manual'; manual.write_bytes(b'not a manual')
        output=self.root/'bank'; output.write_bytes(b'existing bank')
        for name in ('recover_an.py','recover_dx.py'):
            result=subprocess.run([sys.executable,str(Path(__file__).with_name(name)),
                '--rom',str(source),'--manual',str(manual),'--output',str(output),'--force'],
                capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,1)
            self.assertIn('Unsupported input length',result.stderr)
            self.assertEqual(output.read_bytes(),b'existing bank')
        self.assertFalse((self.root/'bank.previous').exists())

    def test_log_cannot_overwrite_input(self):
        source=self.root/'source'; source.write_bytes(b'input')
        result=subprocess.run([sys.executable,str(Path(__file__).with_name('recover_dx.py')),
            '--rom',str(source),'--manual',str(self.root/'manual'),'--log',str(source)],
            capture_output=True,text=True,timeout=10)
        self.assertEqual(result.returncode,1)
        self.assertEqual(source.read_bytes(),b'input')


if __name__ == '__main__':
    unittest.main()
