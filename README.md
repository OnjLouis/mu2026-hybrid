# Mu2026 Hybrid (private candidate)

Mu2026 Hybrid is a separate 32-bit VST2 wrapper over a modified S-MU2000
engine. It uses MU2000 voices first, S-YXG2006LE only for detected MU voice
gaps, and the existing native VL/PVL and SG workers. It does not load XG50.

The local-user guide is [README.html](README.html). This repository contains
wrapper and worker source only. The modified S-MU2000 source is maintained
separately. Do not commit or distribute ROMs, Yamaha binaries, tables, VXDs,
private release tooling, or a local `roms.txt`.

## First-pass verification

- Twenty-three focused CTest cases pass.
- MU dry, reverb, chorus, variation, and insertion 1 process injected audio
  at 44.1 and 48 kHz in the child VST probe.
- The assembled wrapper passes host probes for MU, 2006LE fallback, native VL,
  native SG, two instances, and accessible editor creation.
- 2006LE insertion bypass and distortion yield distinct, audible outputs.

The worker injection path is not a verified emulation of a physical MU2000
PLG board. Live host behavior, long-term performance, effect parameter parity,
and hardware comparisons remain unverified. This is not a public release.

## Build

Build the modified S-MU2000 VST2 target as 32-bit Windows, then build this
repository with CMake and a 32-bit MinGW compiler. Rename the MU engine DLL to
`mu2000-engine.bin` beside `mu2026-hybrid.dll`. Place the generated VL and SG
worker executables there as well. Run `ctest` in the wrapper build directory.

The wrapper requires a valid 4 MB `mu2000_flash.bin` in `roms` or the ROM
directory named by `roms.txt`. The MU engine additionally requires its four
8 MB wave ROMs. Missing or mismatched ROMs are a load failure, not a silent
fallback to XG50.
