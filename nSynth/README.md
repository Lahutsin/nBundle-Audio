# nSynth Build Guide

This repository contains the JUCE version of **nSynth** — a 20-voice,
bi-timbral hybrid digital/analog-modeled polyphonic synthesizer with
binaural stereo voice pairing, dual-layer modulation, 32 custom wavetable
slots, a 64-step polyphonic sequencer, arpeggiator, and studio effects.

![nSynth plugin UI](docs/Plugin.png)

The full design plan, DSP specification, parameter registry, UI spec,
preset-manager design and phased roadmap live in [docs/](./docs):

- [docs/PLAN.md](./docs/PLAN.md) — start here.
- [docs/08-PLAN-REVIEW.md](./docs/08-PLAN-REVIEW.md)
- [docs/09-TEST-SPEC.md](./docs/09-TEST-SPEC.md)
- [docs/01-ARCHITECTURE.md](./docs/01-ARCHITECTURE.md)
- [docs/02-PARAMETERS.md](./docs/02-PARAMETERS.md)
- [docs/03-DSP-SPEC.md](./docs/03-DSP-SPEC.md)
- [docs/04-UI-SPEC.md](./docs/04-UI-SPEC.md)
- [docs/05-PRESET-MANAGER.md](./docs/05-PRESET-MANAGER.md)
- [docs/06-ROADMAP.md](./docs/06-ROADMAP.md)
- [docs/07-QA-AND-VALIDATION.md](./docs/07-QA-AND-VALIDATION.md)

## Current Status

The synthesis core, dual-layer voice architecture, modulation matrix,
wavetable/sequencer/arpeggiator systems, MIDI/MPE engine, compact UI,
multi-core scheduler, and factory content are implemented:

- DDS1: 7-core "super" oscillator (OFF / 1/2 / ON, detune), sine/saw/square/
  triangle/noise/alt waves, morphing, 32 alternative waveforms (`.ws6` I/O).
- DDS2: algorithmic polyBLEP oscillator, NORM/RING/SYNC, LFO range, sub oscillator.
- Cross mod (exponential FM), mixer, 1-pole HPF -> 4-pole ZDF ladder LPF with
  drive 0/1/2, per-stage analog saturation, keytrack, self-oscillation; VCA with ENV2/fixed envelopes, dynamics.
- ENV1 (attack hold, decay hold, loop, inverted, keytrack), ENV2, per-voice LFO1
  (LR phase, HF modes) and LFO2 (trigger modes), drift.
- 20-voice pool shared by UPPER/LOWER: Single/Dual/Split, binaural voice pairs,
  Solo/Legato/Poly 1/Poly 2, unison sizes, portamento, hold/sustain.
- Per-layer chorus (I/II/I+II) -> delay (send/feedback/freeze/sync).
- 2x/4x/8x internal oversampling; Mix + optional Upper/Lower outputs;
  lock-free atomic work-stealing across all CPU cores.
- Compact editor generated from the parameter registry with uniform scaling 50-200 %.
- Performance presets (`.nsynthperf`), layer patches (`.nsynthpatch`), layer init/copy.

Phase 6 adds 128 factory patches, 16 performances and an offline DSP/CPU
analysis target. Verified: `nSynthTests` (59,060 checks, 0 failures), successful
AU validation (178 parameters), and successful AU/VST3 builds. Manual
host/controller/pluginval checks remain open. Current verification status is in
[docs/07-QA-AND-VALIDATION.md](./docs/07-QA-AND-VALIDATION.md), with phase
details in [docs/06-ROADMAP.md](./docs/06-ROADMAP.md).

The project is configured with CMake and C++17. By default it fetches JUCE
from GitHub during configure time.

## Project Layout

- `JUCE/` contains the CMake project.
- `JUCE/Source/Core/` contains the DSP and engine (shared with the tests).
- `JUCE/Source/Tests/` contains the `nSynthTests` unit/integration tests.
- `Presets/` contains the factory `Patch`/`Performance` library.
- `.vscode/tasks.json` contains a ready-to-run VS Code build task.

## Requirements

### All Platforms

- CMake 3.22 or newer
- A C++17 compiler
- Git
- Internet access for the first configure step when `NSYNTH_FETCH_JUCE=ON`

## DSP/CPU analysis

From `nSynth/JUCE`:

```bash
cmake --build build --target nSynthAnalysis -j8
./build/nSynthAnalysis_artefacts/Release/nSynthAnalysis --output ../docs/phase6-analysis-report.md
```

Regenerate the factory library with the `nSynthFactoryGenerator` target; see
[Presets/README.md](./Presets/README.md).

### macOS

- Xcode or Xcode Command Line Tools
- A recent macOS SDK

### Windows

- Visual Studio 2022 with the Desktop development with C++ workload
- MSVC v143 toolset
- Windows 10 or Windows 11 SDK

### Linux

- GCC or Clang with C++17 support
- `make` or Ninja
- Development packages for ALSA, X11, and FreeType

Example packages for Ubuntu or Debian:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config \
  libasound2-dev libjack-jackd2-dev libfreetype6-dev \
  libx11-dev libxext-dev libxinerama-dev libxrandr-dev \
  libxcursor-dev libxcomposite-dev libxrender-dev libglu1-mesa-dev
```

## Build Outputs

- macOS: VST3 and AU
- Windows: VST3
- Linux: VST3

Build artefacts are generated under `JUCE/build/`.

Typical output locations:

- `JUCE/build/nSynth_artefacts/Release/VST3/nSynth.vst3`
- `JUCE/build/nSynth_artefacts/Release/AU/nSynth.component` on macOS
- `JUCE/build/nSynthTests_artefacts/Release/nSynthTests`

`COPY_PLUGIN_AFTER_BUILD` is enabled, so JUCE may also copy the plug-in to
the standard local plug-in folders after a successful build.

## macOS Build

From the repository root:

```bash
cd nSynth
cmake -S JUCE -B JUCE/build -DCMAKE_BUILD_TYPE=Release
cmake --build JUCE/build -j8
```

To clean and rebuild from scratch:

```bash
rm -rf JUCE/build
cmake -S JUCE -B JUCE/build -DCMAKE_BUILD_TYPE=Release
cmake --build JUCE/build -j8
```

## Windows Build

Open a Developer PowerShell for Visual Studio or a normal PowerShell with
Visual Studio in `PATH`, then run:

```powershell
cmake -S JUCE -B JUCE/build -G "Visual Studio 17 2022" -A x64
cmake --build JUCE/build --config Release --parallel
```

To clean and rebuild from scratch:

```powershell
Remove-Item -Recurse -Force JUCE/build
cmake -S JUCE -B JUCE/build -G "Visual Studio 17 2022" -A x64
cmake --build JUCE/build --config Release --parallel
```

If you prefer Ninja on Windows, use a Ninja generator instead of the Visual
Studio generator.

## Tests and validation

```bash
./JUCE/build/nSynthTests_artefacts/Release/nSynthTests          # all tests, exit code 1 on failure
./JUCE/build/nSynthTests_artefacts/Release/nSynthTests --only Engine
./JUCE/build/nSynthTests_artefacts/Release/nSynthTests --dump-params docs/02-PARAMETERS.gen.md
./JUCE/build/nSynthTests_artefacts/Release/nSynthTests --snapshot docs/Plugin.png 1.0
auval -v aumu nSyn NSYA                                          # macOS: AU VALIDATION SUCCEEDED
```

See [docs/07-QA-AND-VALIDATION.md](./docs/07-QA-AND-VALIDATION.md) and
[docs/09-TEST-SPEC.md](./docs/09-TEST-SPEC.md).
