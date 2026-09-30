# Mu2026 Hybrid 0.1.3 tester notes

This is an early 32-bit Windows VST2 build, separate from S-YXG100 Hybrid and
S-YXG2026 Hybrid. It uses a modified S-MU2000 engine for MU2000 voices and
effects, S-YXG2006LE only for MU voice gaps, and native VL/PVL and SG workers.
XG50 is not loaded.

The public update contains code binaries, notices, documentation, and instrument
definitions. It does not contain Yamaha ROMs, firmware, tables, VXDs, or song
files. See README.html for the required local files and ROM layout. Keep your
existing `mu2026.ini` and `roms.txt`; the updater does not replace them.

This update preserves controller setup sent before a channel selects its VL
bank. Previously these early settings could be missing from the VL worker,
including volume, reverb, chorus and CC94 system-variation sends. The reported
imon4xg and imon7xg examples now respond to their variation sends. Ordered
non-note setup is retained; MU note events are not replayed into VL.

Gains, MU firmware voice handling and insertion assignments are unchanged.
Dogroova can sound different from S-YXG100 Hybrid because its VL part uses MU
insertion 2 and the earlier chorus/reverb settings now reach the worker.
That comparison is not proof of exact physical PLG-board effects parity.

The preceding update selects Yamaha's original MU firmware voice path instead of the
experimental native shortcut. This restores drum-controller filtering and the
Feed sound in the reported Driftin Away and Trance examples, and brings the
FatPizz intro much closer to standalone MU. VL, SG and 2006LE injection, gains,
and all four insertion-effect routes remain in place. Existing native=1 INI
entries cannot reactivate the shortcut.

Silent external audio buses now skip unnecessary resampling work without
changing their sample timing or effect tails. A 90-second StarStrp stress
render was about 14% faster with byte-identical output. Local full-length
listening comparisons found no apparent polyphony losses in the reported
examples. Results on other machines and very small buffers still need testing.

For buffering problems, close the host and check that mu2026.ini is beside the
VST DLL with suspend_unused=1 under [engine], then restart the host. The updater
preserves existing INIs and does not add missing keys. See README.html for a
complete example. Do not change VL/SG or 2006LE gain to cure buffering.

Please report the host, sample rate, buffer size, song or MIDI event sequence,
selected voice map, and whether the problem occurs in S-MU2000 or the older
Onj Research hybrids. The editor's Copy report button can provide bounded
state details; please check the report for personal paths before posting it.

Known limits: some mixes can reach the final output ceiling, including the
FatPizz reference in both the preceding and current builds. This controller
fix does not change gain or resolve that existing headroom limit.
SG enters an insertion only when its mixed output belongs to one
assigned MIDI channel. A first VL note can still cause an audio-thread setup
spike. Low-buffer live use, extended sessions, simultaneous insertion
assignments to one part, and physical PLG-board equivalence remain unverified.
