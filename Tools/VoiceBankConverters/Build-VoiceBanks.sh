#!/bin/sh
# Native Linux launcher; Wine is needed only for the synth, not conversion.
set -u
root=$(CDPATH='' cd "$(dirname "$0")" && pwd) || exit 1
mode=''
force=''
noninteractive=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --mode) [ "$#" -ge 2 ] || { echo 'Missing --mode value' >&2; exit 2; }; mode=$2; shift 2 ;;
        --force) force='--force'; shift ;;
        --non-interactive) noninteractive=1; shift ;;
        --help) echo 'Usage: sh Build-VoiceBanks.sh [--mode an|dx|both|setup] [--non-interactive] [--force]'; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done
if [ -z "$mode" ]; then
    if [ "$noninteractive" -eq 1 ] || [ ! -t 0 ] || [ ! -t 1 ]; then
        echo 'Specify --mode an, dx, both or setup for unattended use.' >&2
        exit 1
    fi
    printf 'Mu2026 Voice Bank Tools 1.0.0\n1. Convert AN\n2. Convert DX\n3. Convert both\n4. Set up local Python dependencies (downloads packages)\nChoose 1 to 4: '
    read -r answer || exit 1
    case "$answer" in 1) mode=an ;; 2) mode=dx ;; 3) mode=both ;; 4) mode=setup ;; *) echo 'No valid option selected.' >&2; exit 1 ;; esac
fi
case "$mode" in an|dx|both|setup) ;; *) echo 'Unsupported mode.' >&2; exit 2 ;; esac
state="$root/user"
python="$state/venv/bin/python"
mkdir -p "$state" || exit 1
log="$state/session.log"
if [ "$mode" = setup ]; then
    echo 'Installing pinned packages from PyPI into user/venv. System Python is not modified.'
    if [ ! -x "$python" ]; then
        command -v python3 >/dev/null 2>&1 || { echo 'Python 3.10+ is required. See README.html.' >&2; exit 1; }
        python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3,10) else 1)' || { echo 'Python 3.10+ is required.' >&2; exit 1; }
        python3 -m venv "$state/venv" >"$log" 2>&1 || { cat "$log"; echo 'Install your distribution python3-venv package if needed.' >&2; exit 1; }
    fi
    "$python" -m pip install --disable-pip-version-check --no-cache-dir -r "$root/requirements.txt" >"$log" 2>&1 || { cat "$log"; exit 1; }
    cat "$log"
    echo 'Local setup complete. Run this launcher again to convert.'
    exit 0
fi
[ -x "$python" ] || { echo 'Run --mode setup first. Nothing has been converted.' >&2; exit 1; }
kinds=$mode
[ "$mode" != both ] || kinds='an dx'
for kind in $kinds; do
    if [ "$kind" = an ]; then rom=IC3_XZ970A0_MBM29F800BA.bin; manual=PLG150AN.pdf; else rom=PLG150-DX.BIN; manual=PLG150DXE.pdf; fi
    for file in "$rom" "$manual"; do
        [ -f "$root/input/$file" ] || { echo "Required input missing: input/$file. See README.html." >&2; exit 1; }
    done
done
: >"$log"
for kind in $kinds; do
    if [ "$kind" = an ]; then rom=IC3_XZ970A0_MBM29F800BA.bin; manual=PLG150AN.pdf; else rom=PLG150-DX.BIN; manual=PLG150DXE.pdf; fi
    if "$python" "$root/recover_$kind.py" --rom "$root/input/$rom" --manual "$root/input/$manual" --output "$root/plg-$kind-voices.bin" --log "$state/$kind.log" $force >>"$log" 2>&1; then
        cat "$state/$kind.log"
    else
        cat "$log"
        exit 1
    fi
done
echo 'Conversion complete. Close the Wine audio host, place the new banks beside mu2026-hybrid.dll, then reopen it.'
