# nComp Validation And QA

This repository now includes two validation layers:

1. Offline analysis via the `nCompAnalysis` console target.
2. Manual host QA for behaviour that cannot be meaningfully automated from a standalone DSP harness.

## Offline Analysis

Build and run the analysis harness from the repository root:

```bash
cmake -S JUCE -B JUCE/build -DCMAKE_BUILD_TYPE=Release
cmake --build JUCE/build --target nCompAnalysis -j8
./JUCE/build/nCompAnalysis_artefacts/nCompAnalysis --output JUCE/build/ncomp-analysis-report.md
```

The harness reports:

- alias-fold stress from coherent high-frequency harmonic folds
- THD / THD+N under reference bus-compressor settings
- SMPTE-style IMD from 60 Hz + 7 kHz tones
- program-release recovery times
- stereo image stability after off-centre transients
- automation robustness under fast parameter moves
- sample-rate switching stability at 44.1 / 48 / 96 kHz
- CPU headroom in blocks-per-second / realtime-factor terms

## Provisional Acceptance Gate

Treat the plug-in as market-ready only if all of these are true at the same time:

- alias-fold stress is at or below `-55 dBr`
- THD+N is at or below `1.2%` in the reference 1 kHz stress test
- IMD is at or below `0.20%`
- stereo image drift stays at or below `0.50 dB`
- automation stress remains finite with no obvious zippering bursts
- CPU headroom stays above `3x realtime` on the reference machine
- host QA and blind listening gates both pass

These are engineering gates, not marketing claims. If a listening pass contradicts a metric pass, the listening pass wins and the DSP needs another iteration.

## Host QA Matrix

Run the plug-in manually in the hosts you care about and verify these behaviours:

- instantiation as VST3 on every supported platform
- instantiation as AU on macOS
- correct latency compensation when bypass toggles and when `SC Listen` is enabled or disabled
- stable automation playback for `Threshold`, `Mix`, `Link %`, `SC HPF`, and `SC Tilt`
- preset recall from both the host preset browser and the in-plugin preset menu
- A/B slot changes while transport is running
- external sidechain routing plus `SC Listen` audition path
- sample-rate changes while the session is open
- mono-to-stereo and stereo-to-stereo routing edge cases
- state restore after project close / reopen

## Listening Passes

Use the factory presets as starting points, then run focused listening checks on:

- full mix glue at 1-3 dB GR
- drum bus punch at 3-6 dB GR
- vocal levelling with asymmetric stereo material
- ducking from an external kick sidechain
- automation ramps during exposed cymbal tails and reverbs

## Blind A/B Protocol

Run a blind comparison against at least two strong references in the same role class:

- level-match each candidate to within `0.1 dB`
- hide plug-in names and randomise order for every pass
- evaluate at least these scenarios: mix glue, drum bus, vocal levelling, external-sidechain ducking
- use a minimum of `10` trials per scenario
- score tone, depth, stereo stability, release naturalness, and transient handling separately
- do not accept “top-tier” positioning unless nComp wins or ties the preferred choice in most scenarios across multiple listeners

The offline harness gives objective baselines. The host QA pass is still required before calling the plug-in production-ready.