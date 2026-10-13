"""Shared input checks and transactional output for the public bank converters."""
import argparse
from contextlib import redirect_stdout
import hashlib
import os
from pathlib import Path
import shutil
import sys
import tempfile

VERSION = "1.0.0"
ROM_BYTES = 1048576
MAX_MANUAL_BYTES = 2 * 1024 * 1024
MAX_BANK_BYTES = 2 * 1024 * 1024
INPUTS = {
    "an": (
        "6796ec4244c24957f931106b95f3746e439e52955a15db61bc0da76fe7231d38",
        "e785f4bba6fe0d5cac518ad77abf6d2dbd5267393e1c3d8a32f901bcc9e5df3f",
        "f631196b177dadda4c850175a7305cdbdfe15dcc4cdb87db18ce0c44e19667db",
    ),
    "dx": (
        "bb09836a2dc13f1fecb0bf5c64c26f5cd86da98967c1d5278417d157043a73dd",
        "63b4c68b8c7d90a200dfa6f3fc0aa72b204be6bd2ac0563bfa0d2ea9edebe47d",
        "897e55a7d0706edcaa7099f7e2f00b632281f9cf1477acdd3b247d56be45d87a",
    ),
}


class ConversionError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise ConversionError(str(message))


def read_checked(path, expected_hash, maximum, exact=None):
    path = Path(path)
    require(path.is_file(), "Input file not found: " + str(path))
    with path.open("rb") as source:
        data = source.read(maximum + 1)
    require(len(data) <= maximum, "Input exceeds the supported size: " + path.name)
    if exact is not None:
        require(len(data) == exact, "Unsupported input length: " + path.name)
    require(hashlib.sha256(data).hexdigest() == expected_hash,
            "Unsupported or changed input: " + path.name + ". See README.html for the supported hashes.")
    return data


def write_bank(output, data, force=False, protected=()):
    output = Path(output).absolute()
    require(len(data) <= MAX_BANK_BYTES, "Generated bank exceeds the size limit")
    require(output.parent.is_dir(), "Output folder does not exist")
    require(not output.is_symlink(), "Output must not be a symbolic link")
    backup = output.with_name(output.name + ".previous")
    forbidden = {Path(p).resolve() for p in protected}
    require(output.resolve() not in forbidden and backup.resolve() not in forbidden,
            "Output or backup would overwrite an input file")
    if output.exists():
        require(output.is_file() and output.stat().st_size <= MAX_BANK_BYTES,
                "Existing output is not a supported bank-sized file")
        if output.read_bytes() == data:
            return "Already current: " + output.name
        require(force, "Output already exists; use --force to replace it with a .previous backup")
        require(not backup.is_symlink() and (not backup.exists() or backup.is_file()),
                "Backup target is not a regular file")
    lock = output.with_name(output.name + ".lock")
    try:
        lock_fd = os.open(lock, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError as error:
        raise ConversionError("Another conversion or a stale lock exists: " + lock.name) from error
    os.close(lock_fd)
    staged = backup_stage = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, prefix=".bank-", delete=False) as stream:
            staged = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        if output.exists():
            require(force, "Output appeared during conversion; nothing was replaced")
            require(output.is_file() and not output.is_symlink()
                    and output.stat().st_size <= MAX_BANK_BYTES, "Existing output changed unsafely")
            with tempfile.NamedTemporaryFile(dir=output.parent, prefix=".backup-", delete=False) as stream:
                backup_stage = Path(stream.name)
                with output.open("rb") as old:
                    shutil.copyfileobj(old, stream)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(backup_stage, backup)
            backup_stage = None
            os.replace(staged, output)
        elif os.name == "nt":
            os.rename(staged, output)  # Windows rename refuses a concurrently created target.
        else:
            os.link(staged, output)  # Atomic no-clobber publication on Unix.
            staged.unlink()
        staged = None
        return "Created: " + output.name
    finally:
        for path in (staged, backup_stage, lock):
            if path is not None:
                path.unlink(missing_ok=True)


class Tee:
    def __init__(self, console, log):
        self.console, self.log = console, log

    def write(self, text):
        self.console.write(text)
        if self.log:
            try:
                self.log.write(text)
            except OSError:
                self.log = None
        return len(text)

    def flush(self):
        self.console.flush()
        if self.log:
            try:
                self.log.flush()
            except OSError:
                self.log = None


def main_cli(kind, recover):
    parser = argparse.ArgumentParser(description="Build a Mu2026 " + kind.upper() + " voice bank from supplied firmware and manual.")
    parser.add_argument("--version", action="version", version=VERSION)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--manual", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("plg-" + kind + "-voices.bin"))
    parser.add_argument("--force", action="store_true", help="Replace a different existing bank, retaining one .previous backup")
    parser.add_argument("--log", type=Path, help="Optional single overwritten text log")
    args = parser.parse_args()
    log = None
    try:
        protected = {args.rom.resolve(), args.manual.resolve(), args.output.resolve(),
                     args.output.with_name(args.output.name + ".previous").resolve()}
        if args.log:
            require(args.log.resolve() not in protected and not args.log.is_symlink(),
                    "Log would overwrite an input, bank or backup")
            if args.log.exists():
                require(not any(path.exists() and args.log.samefile(path)
                                for path in (args.rom, args.manual, args.output,
                                             args.output.with_name(args.output.name + ".previous"))),
                        "Log aliases an input, bank or backup")
            log = args.log.open("w", encoding="utf-8")
        with redirect_stdout(Tee(sys.stdout, log)):
            print("Mu2026 voice-bank converter " + VERSION + ": " + kind.upper())
            rom_hash, manual_hash, bank_hash = INPUTS[kind]
            rom = read_checked(args.rom, rom_hash, ROM_BYTES, ROM_BYTES)
            read_checked(args.manual, manual_hash, MAX_MANUAL_BYTES)
            output = recover(rom, args.manual)
            require(hashlib.sha256(output).hexdigest() == bank_hash,
                    "Recovered bank differs from the verified reference; no bank was written")
            print(write_bank(args.output, output, args.force, (args.rom, args.manual)))
            print("SHA-256: " + bank_hash)
        return 0
    except (ConversionError, OSError, ImportError, ValueError) as error:
        message = "Conversion failed: " + str(error)
        print(message, file=sys.stderr)
        if log:
            try:
                log.write(message + "\n")
            except OSError:
                pass
        return 1
    finally:
        if log:
            try:
                log.close()
            except OSError:
                pass
