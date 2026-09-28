# Mu2026 Hybrid 0.1.0 tester notes

This is an early 32-bit Windows VST2 build, separate from S-YXG100 Hybrid and
S-YXG2026 Hybrid. It uses a modified S-MU2000 engine for MU2000 voices and
effects, S-YXG2006LE only for MU voice gaps, and native VL/PVL and SG workers.
XG50 is not loaded.

The public update contains code binaries, notices, documentation, and instrument
definitions. It does not contain Yamaha ROMs, firmware, tables, VXDs, or song
files. See README.html for the required local files and ROM layout. Keep your
existing `mu2026.ini` and `roms.txt`; the updater does not replace them.

In the first local listening checks, FatPizz's distorted VL part matched the
reference and remained clean, ForYou and SG_yuki were balanced, and StarStrp
played smoothly in Foobar while another application was in front. Automated
host and engine probes cover 44.1 and 48 kHz, including MU insertion 2.

Please report the host, sample rate, buffer size, song or MIDI event sequence,
selected voice map, and whether the problem occurs in S-MU2000 or the older
Onj Research hybrids. The editor's Copy report button can provide bounded
state details; please check the report for personal paths before posting it.

Known limits: external audio insertions 3 and 4 are not supported. SG enters
insertion 2 only when its mixed output belongs to one assigned MIDI channel.
Low-buffer live use, extended sessions, and physical PLG-board equivalence have
not been fully verified.
