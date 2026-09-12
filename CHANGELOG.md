# Changelog

## v2.0.0 — 2026-09-12

Klein Bottle Experimental v2.0 focuses the instrument around fewer, more effective controls and more distinctive topology models.

### Sound and workflow

- Reduced the interface to Topology, Exciter, Decay, Tone, Motion, Width and Level.
- Added eight factory presets, also exposed as host programs.
- Focused topology selection on Klein Bottle, Möbius Band and Membrane.
- Added topology-specific excitation, pickup geometry, stereo processing and spectral response.
- Added subtle tuned fundamental reinforcement for clearer correspondence between MIDI note and perceived pitch.
- Rebalanced keyboard response to remove the upper-register level drop above C4.
- Made Tone a broad spectral control that affects every exciter.

### Reliability and interface

- Removed excitation-time heap allocation from the audio callback.
- Replaced the locked GUI MIDI queue with a fixed-capacity FIFO.
- Moved Panic handling safely onto the audio thread.
- Reduced the minimum editor size and added concise control tooltips.
- Centralised the displayed version on the CMake project version.

### Formats

- Windows x64: VST3 and Standalone.
- macOS universal: VST3 and Standalone.
