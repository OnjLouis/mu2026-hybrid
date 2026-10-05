# Mu2026 Hybrid

Mu2026 Hybrid is a separate 32-bit VST2 wrapper over a modified S-MU2000
engine. It uses MU2000 voices first, S-YXG2006LE only for detected MU voice
gaps, and the existing native VL/PVL and SG workers. It does not load XG50.

Research prerelease 0.2.2 integrates native six-operator FM DX voices into the same
Mu2026 VST identity. It is an opt-in preview, not a stable update. Recovered voice parameters
are external, private assets; the Google MSFA core is Apache-2.0 with its
license and modification notice under ThirdParty/msfa. See the HTML guide
for supported controllers and remaining PLG150-DX compatibility gaps.

It also includes a native, parameter-compatible PLG150-AN listening candidate
when the separately supplied `plg-an-voices.bin` is present. This is **not VOP3
chip emulation**. The recovered 256 presets/848 selections feed MU effects,
but synthesis calibration, complete Free EG, arpeggiation, full unison, morphing and
custom voice dumps remain incomplete. See the HTML manual for the supported
controls, bank behaviour, attribution and explicit approximation limits.

The isolated AN filter follow-up restores velocity/key-position matrix inputs
and filter velocity sensitivity. Filter envelope timing, depth and resonance
use explicitly documented candidate calibrations, not recovered Yamaha DSP
curves. RnB pitch is accepted; its filter character still needs listening
comparison. These changes do not alter MU, DX, VL/PVL, SG or 2006LE engines.

The isolated AN follow-up implements recorded Free EG tracks with loop/time
selection, LFO waveform shapes and selected PWM sources. Saw pulse width now
changes its harmonics rather than being ignored; this corrects the measured
octave error in the RnB bass comparison. This is not a hardware-parity claim.
Trance's intro, Old_Tek's talking timbre and ProgRock's opening require renewed
listening after the oscillator and stored-pattern changes below.

The oscillator candidate connects both VCO Edge controls, including recorded
Free EG, matrix modulation and received Direct edits. Intermediate Edge uses
a spectral crossfade, not recovered Yamaha DSP. It preserves the reference-tested
saw pitch law, distinguishes unsynchronised saw2, separates Sync Pmod from FM
algorithm selection, retains fractional-sample sync timing, and implements the
documented 60-degree LFO2-phase PWM source. Free-running LFOs use a continuous
part clock; Key-on mode still resets. The upper documented resonance range no
longer flattens at the old damping floor. Inner waves, oscillator/filter scaling
and LFO speed remain approximations. New 70-second comparisons require listening;
these changes alone do not establish that the reported timbre faults are fixed.

## Listening findings and help wanted

Local listening found Trance closest to its hardware recording after restoring
HardNoiz's stored eight-step phrase. ProgRock, Old_Tek and RnB still sound much
like the preceding candidate. Their audible faults are **not resolved**, even
where the decoded samples differ. This preview is not hardware-equivalent AN.

Please compare Trance's opening rhythm and pitched pattern, ProgRock's opening
sync/FM and amplifier character, Old_Tek's speech-like modulation, and RnB's
bass pitch, filter sweep and resonance. Recordings of isolated notes and controlled
parameter sweeps from a PLG150-AN would help distinguish oscillator, filter,
envelope and effect differences. The [HTML guide](README.html#listening-feedback)
explains a repeatable comparison and what to include in a report. Do not upload
ROMs, firmware, recovered parameter banks or copyrighted recordings to issues.

The stable release remains 0.1.3. The signed updater deliberately ignores
prereleases; install this preview manually into a separate folder and keep your
stable copy. Public downloads contain code only. AN and recovered DX presets
require separately supplied, legally obtained parameter banks.

Single stored voice patterns now play their recovered notes, velocities, rests,
gate lengths and controller steps. Normal and Shift & Normal keyboard modes,
C2 transposition, split point, forward/backward loops, swing and Hold are supported.
Overlapping steps retain separate voices within the existing five-note part limit;
trigger release, panic, reset and patch changes have bounded stop paths. Trance's
HardNoiz preset enables an eight-step pattern, which was previously ignored.
User-pattern selection, alternate loops, arpeggiation and external MIDI-clock
timing remain unsupported; the latter uses the documented candidate 120-BPM fallback.

The integrated control path connects pulse-width/depth, HPF and
feedback matrix destinations, received CC/aftertouch Direct edits, independent
FM depth/wave selectors, and VCO2 sine/triangle cross-modulation. Untouched
Direct knobs preserve factory settings. FM and cross-modulation depth laws
remain approximations. Twenty-second reference comparisons still show the
three demo discrepancies; these control-path fixes are not evidence of hardware
parity or a complete audible fix. Low-pass calibration and engine gains are
unchanged.

Manual-backed follow-up corrections put AN's separate 6 dB/octave HPF before
the VCF, make LP18 a three-pole response, take feedback after the VCA, and put
board distortion after the VCA. Amp Type Off retains distortion drive and the
wet-path LPF with documented cutoff selections. Exact filter/distortion/cabinet
curves are still approximate; demo timbre parity is not established. DX adds
note/velocity limits, sostenuto with sustain/reset interaction, and Omni-mode
All Notes Off handling. Velocity-depth/offset curves and receive switches are
not guessed. Existing engine gains are unchanged.

The user guide is [README.html](README.html). This repository contains wrapper
and worker source only. Its MU2000 engine is the
[Mu2026 S-MU2000 fork](https://github.com/OnjLouis/mu2026-smu-engine) of
[tarboh's S-MU2000](https://github.com/tarboh/S-MU2000), distributed under the
BSD 3-Clause license with its third-party notices. The wrapper retains the MIT
license of S-YXG2026 Hybrid. Neither project is affiliated with Yamaha.

This source does not include Yamaha ROMs, firmware, tables, VXDs, MIDI songs,
or a local `roms.txt`. Users must provide the hardware-derived data from their
own authorized sources. Do not upload these files to GitHub issues or releases.

The [QWS](Definitions/QWS/inst_mu2026_hybrid.ini) and
[Reaper](Definitions/Reaper/Mu2026%20Hybrid.reabank) definitions are included.
The inherited MU definition also names PLG expansion voices; listing a voice
does not prove that its optional expansion board is emulated.

`Update Yamaha Hybrids.cmd` checks GitHub for a signed Mu2026 code update and
can replace only the files listed in its signed manifest. It leaves local ROMs,
Yamaha files, `mu2026.ini`, and `roms.txt` untouched. Close audio hosts before
installing an update. The rollback ZIP is kept outside the plugin directory.
For a fresh install, copy [Config/mu2026.ini](Config/mu2026.ini) beside the VST
DLL. These are the settings used in the Foobar performance and gain checks.

## First-pass verification

- Twenty-seven focused CTest cases pass, including firmware-mode selection,
  rejection cleanup, and pre-bank VL controller setup with ROM-free mocks.
- MU dry, reverb, chorus, variation, and all four insertions process injected
  audio at 44.1 and 48 kHz in the child probe.
- The assembled wrapper passes host probes for MU, 2006LE fallback, native VL,
  native SG, two instances, and accessible editor creation.
- 2006LE insertion bypass and distortion yield distinct, audible outputs.
- A full FatPizz render restores its insertion-2 distorted VL part; the user
  confirmed its level and sound. Unaffected ForYou and SG_yuki renders remain
  byte-identical to the prior candidate. An isolated SG route and a 2006LE
  fallback voice also respond to insertion-2 assignment.

The worker injection path is not a verified emulation of a physical MU2000
PLG board. An insertion is applied to SG only when the worker reports a single
assigned MIDI channel; a multi-channel SG mix cannot be separated afterward.
Long-term performance, very low-buffer live use, effect parameter parity, and
hardware comparisons remain unverified.

Version 0.1.3 retains ordered non-note controller setup before VL bank selection.
This restores early volume, reverb, chorus and CC94 variation sends in the
reported files without changing gains or replaying MU notes into VL. Controlled
variation on/off comparisons differ at 44.1 and 48 kHz; local listening confirms
the improvement. Exact physical-hardware effect parity still needs comparison.

Version 0.2.2 forwards CC91/93/94 to MU for VL parts
assigned to insertions 2-4, including controller values received before a late
insertion assignment. System effects receive the processed insertion output,
without duplicate unprocessed worker sends. Non-inserted parts keep their
existing path and VL gain is unchanged. A previous small FatPizz variation
on/off difference did not prove that its post-insertion delay was correct;
FatPizz's restored delay is now confirmed by listening, while exact
physical-hardware levels remain unverified.

The matching MU engine incorporates upstream S-MU2000 through commit
`67c550e5bc58110dd527a745083e4e8aa25af919` (merged pull request 121), including
its sampler waveform synthesis and subsequent fixes. Sampler, resampler,
ROM-directory, SysEx-memory and external-effects checks passed. This does not
replace the approximate AN engine with hardware emulation, supply new ROMs,
or establish screen-reader accessibility of the upstream sampler editor.

Version 0.1.2 selects Yamaha's original firmware voice path at load, ignoring
the older native=1 shortcut. This preserves MU voice/controller behaviour while
keeping external VL/SG/2006LE effect routing. Local comparisons against original
MU found no apparent polyphony losses in the reported files. A silent-bus
resampling optimization made a 90-second stress render about 14% faster with
byte-identical audio. Event processing and first-worker preparation can still
cause short deadline overruns; this is not a guarantee of glitch-free use in
every host or at every buffer size.

For buffering guidance and a complete INI example, see the user guide's
Performance settings section. Set suspend_unused=1 under [engine] beside the
VST DLL and restart the host; updates preserve rather than rewrite user INIs.

## Build

Build the modified S-MU2000 VST2 target from the linked fork as 32-bit Windows,
then build this repository with CMake and a 32-bit MinGW compiler. Rename the MU engine DLL to
`mu2000-engine.bin` beside `mu2026-hybrid.dll`. Place the generated VL and SG
worker executables there as well. Run `ctest` in the wrapper build directory.

The wrapper requires a valid 4 MB `mu2000_flash.bin` in `roms` or the ROM
directory named by `roms.txt`. The MU engine additionally requires its four
8 MB wave ROMs. Missing or mismatched ROMs are a load failure, not a silent
fallback to XG50.
