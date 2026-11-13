# MU2000 Desktop

MU2000 Desktop is the accessible, native frontend for the base S-MU2000
engine. It is separate from the Mu2026 Hybrid VST2 and Voice Bank Tools.
It provides a standalone and VST3 on Windows x64, plus an Audio Unit on
Apple Silicon Macs. Yamaha ROMs, project files and expansion content are
not included. See the [manual](Manual.html) for setup and navigation.

## Build

Use CMake 3.22 or later with a C++20 toolchain. JUCE 9.0.5 is fetched by
CMake. Supply these dependency checkouts or source trees:

- `SMU_SOURCE`: [S-MU2000 fork](https://github.com/OnjLouis/mu2026-smu-engine/tree/5ed88bf2afd9c4aae83b3cc123c26be6691e40b9)
  at commit `5ed88bf2afd9c4aae83b3cc123c26be6691e40b9`.
- `CANVAS_CONTROLS`: `Source` from
  [Sound Canvas Bridge](https://github.com/OnjLouis/sound-canvas-bridge/tree/a67052aa6348d98d4c086e9c06d524b41f579faa).
- `ACCESSIBLE_CONTROLS`: `plugin/Source` from
  [Hypersonic 2 Port](https://github.com/masonasons/hypersonic2-port/tree/a8113d7e35bc882eb6b7342a8281b5c8377808b2).
  Only the three frontend headers are used, not its engine or libraries.

Build `OnjMu2000_VST3` and `OnjMu2000_Standalone`. Mac builds also provide
`OnjMu2000_AU` and `Mu2000MacApplication` (includes the updater helper).
Windows builds with the MU2000 **public** RSA verification key supplied as
`MU_UPDATE_PUBLIC_KEY` also provide `Mu2000WindowsApplication`. The platform
installer targets are intended for signed distributions; an ordinary local
build is not a trusted update. No private signing files belong in this tree.

Windows uses the GPLv3 option of the Steinberg ASIO SDK as well as WASAPI.
CMake downloads a hash-pinned SDK, or accepts its root as `ASIO_SDK_DIR`.

Desktop release tags use `mu2000-vMAJOR.MINOR.PATCH`. The updater requires the
exact platform asset. Windows verifies the installer signature and the
embedded manifest; macOS requires the designated Developer ID, notarised image
and notarised app with matching versions. Audio hosts are not forcibly closed.

The distributed JUCE application and plug-ins use GNU AGPLv3. The engine
retains its upstream BSD 3-Clause licence and the reused Canvas code retains
its MIT notice. Keep the dependency revisions and licence notices with the
source and binaries.
