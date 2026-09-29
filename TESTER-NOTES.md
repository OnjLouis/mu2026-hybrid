# Mu2026 Hybrid 0.1.1 tester notes

This is an early 32-bit Windows VST2 build, separate from S-YXG100 Hybrid and
S-YXG2026 Hybrid. It uses a modified S-MU2000 engine for MU2000 voices and
effects, S-YXG2006LE only for MU voice gaps, and native VL/PVL and SG workers.
XG50 is not loaded.

The public update contains code binaries, notices, documentation, and instrument
definitions. It does not contain Yamaha ROMs, firmware, tables, VXDs, or song
files. See README.html for the required local files and ROM layout. Keep your
existing `mu2026.ini` and `roms.txt`; the updater does not replace them.

This update corrects Shellshk's drum pitch and CC94-driven variation delay,
preserves external-FX headroom at 48 kHz, and exposes MU insertion effects 3
and 4 to VL, SG, and fallback voices. Shellshk's drums and delay were checked
in live Foobar playback. Engine probes distinguish all four insertion effects
from bypass at both 44.1 and 48 kHz. A VL-only FatPizz render changes when
its CC94 delay send is removed; its exact wet level against hardware remains
unverified. The silent sampling-bank Voice001-Voice256 entries have been
removed from the QWS and REAPER definitions.

Please report the host, sample rate, buffer size, song or MIDI event sequence,
selected voice map, and whether the problem occurs in S-MU2000 or the older
Onj Research hybrids. The editor's Copy report button can provide bounded
state details; please check the report for personal paths before posting it.

Known limits: SG enters an insertion only when its mixed output belongs to one
assigned MIDI channel. A first VL note can still cause an audio-thread setup
spike. Low-buffer live use, extended sessions, simultaneous insertion
assignments to one part, and physical PLG-board equivalence remain unverified.
