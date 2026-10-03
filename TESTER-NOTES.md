# Mu2026 Hybrid 0.2.1 research prerelease

This opt-in preview adds native DX and an approximate AN engine to the existing
32-bit Windows VST2 instrument. Stable 0.1.3 remains available and the signed
updater ignores this prerelease. Test in a separate plugin folder. It retains
the same VST identity, so choose the preview explicitly in your host.

## Findings so far

- Trance's opening is closer to its hardware recording after restoring the
  HardNoiz preset's eight-step voice pattern. Previously only the incoming
  held note played.
- ProgRock, Old_Tek and RnB still sound much like the preceding candidate.
  Their timbre differences are not solved. RnB's bass pitch correction is
  accepted locally, but its filter still does not match the recording.
- AN is a parameter-compatible approximation, not Yamaha VOP3 chip emulation.
  Filter, oscillator, envelope, modulation and amplifier transfer laws still
  need controlled hardware comparisons. Correct routing is not enough to
  guarantee the same sound.

## What to listen for

Compare Trance's opening pattern, ProgRock's opening sync/FM and amplifier
character, Old_Tek's speech-like vowel movement, and RnB's bass filter and
resonance. Use identical MIDI and reset both instruments first. Give the
passage, bank/program, expected sound, host, rate and buffer size. Identify
your reference hardware or recording. The HTML guide has a comparison table
and instructions for useful isolated-note and parameter-sweep tests.

Please share only recordings or examples you own or have permission to share.
Do not upload Yamaha ROMs, firmware, recovered parameter banks or third-party
reference recordings to GitHub issues. Review diagnostic reports for private
paths before posting them.

## Implemented in the preview

AN includes recovered preset selection, selected controller-matrix and Direct
edits, recorded Free EG tracks, oscillator Edge/PWM, continuous reset-off LFO,
sync pitch selection, separate 6-dB HPF before VCF, three-pole LP18, post-VCA
feedback and amplifier distortion, and single stored voice-pattern playback.
Normal and Shift & Normal, split/C2 transposition, forward/backward looping,
notes/rests/velocities, swing, gate overlap, controller steps and Hold are
implemented. Long gates share the existing five-voice limit per part.

AN arpeggiation, user-pattern selection, alternate sequencer loops, external
MIDI-clock timing, full unison, morphing, complete controller-matrix handling,
custom dumps and exact inner-wave/amp/filter calibration remain gaps. Clock
mode currently falls back to 120 BPM. Five notes per channel is a software
extension, not the original board's shared five-note limit.

DX uses Google's Apache-2.0 MSFA six-operator FM core with recovered external
parameters. It supports held-note preservation across patch changes, tuning,
selected assigned pitch controllers, sustain/sostenuto, note/velocity limits,
operator masks and checked voice dumps. It is not a complete PLG150-DX emulator:
AM, EG bias, filters, portamento, ACED, exact phase/LFO behaviour, receive
switches and undocumented controller scaling remain incomplete.

Both engines feed MU dry/reverb/chorus/variation and all four assigned insertion
inputs without an additional worker. Existing MU, VL/PVL, SG and 2006LE gains
are unchanged. XG50 is not loaded. Dogroova's native VL patch-change cutoff
is not claimed fixed by the DX held-note work.

## Data and installation

The public download contains code binaries, licenses, documentation and QWS/
Reaper definitions only. It contains no Yamaha data or demo MIDI recordings.
You must supply legally obtained MU ROMs and the existing Yamaha dependencies.
Recovered DX presets require `plg-dx-voices.bin`, and AN requires
`plg-an-voices.bin`, beside the VST DLL; neither bank is in this download.
Without those banks, this preview does not make DX/AN factory sounds available.
The instrument definitions name voices but do not prove board emulation.

Preserve your `roms.txt` and `mu2026.ini`. Copy the supplied default INI only
for a fresh separate install. For buffering, put `suspend_unused=1` under
`[engine]` beside the DLL and restart the host. Leave the accepted gains alone;
gain changes are not a buffering fix. Rollbacks belong outside scanned plugin
folders so hosts do not mistake them for duplicate instruments.

## Evidence and remaining uncertainty

27 automated test suites pass. Short probes of all 256 AN presets remained
finite/bounded, and earlier short sweeps covered 1,602 recovered DX selections.
Four 70-second full-mix AN comparisons had no full-scale output samples.
Final paired 20-second MU, DX, SG, PVL and 2006LE renders were byte-identical
to the preceding source candidate. This does not prove audible AN correctness,
long-session stability, physical PLG effect parity or very low-buffer playback.
Some setup/event blocks exceed their audio deadline. Some existing full mixes
can reach the final output ceiling. Keep stable 0.1.3 for production use.

Credits and the full supported-control list are in README.html. Thanks to
everyone providing detailed MIDI and hardware comparison feedback.
