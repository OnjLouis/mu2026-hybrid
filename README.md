# Mu2026 Hybrid

Mu2026 Hybrid is a separate 32-bit VST2 wrapper over a modified S-MU2000
engine. It uses MU2000 voices first, S-YXG2006LE only for detected MU voice
gaps, and the existing native VL/PVL and SG workers. It does not load XG50.

The user guide is [README.html](README.html). This repository contains wrapper
and worker source only. Its MU2000 engine is the
[Mu2026 S-MU2000 fork](https://github.com/OnjLouis/mu2026-smu-engine) of
[tarboh's S-MU2000](https://github.com/tarboh/S-MU2000), distributed under the
BSD 3-Clause license with its third-party notices. The wrapper retains the MIT
license of S-YXG2026 Hybrid. Neither project is affiliated with Yamaha.

This source does not include Yamaha ROMs, firmware, tables, VXDs, MIDI songs,
or a local `roms.txt`. Users must provide the hardware-derived data from their
own authorized sources. Do not upload these files to GitHub issues or releases.

The [QWS](Definitions/QWS/Mu2026%20Hybrid.ini) and
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

- Twenty-four focused CTest cases pass, including firmware-mode selection and
  rejection cleanup with a ROM-free mock engine.
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
