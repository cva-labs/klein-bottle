# Klein Bottle Experimental (v2.0) — CVA Labs

A **physical-modelling synthesizer** whose resonator is a digital waveguide mesh living on the surface of a **Klein bottle** — a closed surface with no inside or outside. Struck, plucked, bowed or blown, every note vibrates across a topology that cannot exist in 3D space.

![format](https://img.shields.io/badge/format-VST3%20%2B%20Standalone-blue) ![license](https://img.shields.io/badge/license-AGPL--3.0--or--later-green) ![platform](https://img.shields.io/badge/platform-Windows%20%7C%20macOS-lightgrey)

**Downloads:** see [Releases](https://github.com/cva-labs/klein-bottle/releases) — self-contained builds for **Windows (x64)** and **macOS** (VST3 + Standalone).

---

## How it works — from math to sound

### 1. The resonator: a wave equation on a strange surface

Each voice owns a **2-D rectilinear waveguide mesh**: a grid of 4-port scattering junctions that discretises the wave equation (FDTD). Per node, per sample:

```
u[n+1](i,j) = 0.5·( u[i±1,j] + u[i,j±1] )  −  σ·u[n](i,j)  −  (1−σ)·u[n−1](i,j)
```

At the special **Courant number** `c·dt/dx = 1/√2` this update is *exactly* the lossless scattering law of a 4-way waveguide junction — no interpolation, no numerical smearing. The grid *is* the instrument: energy injected at one node propagates, reflects, interferes and rings as it would in a real 2-D plate.

**→ You hear:** a dense, evolving modal spectrum — hundreds of modes per note, the way a real membrane or gong rings, rather than a filtered oscillator.

### 2. Damping ↔ decay time

`σ` is a per-sample viscous (velocity) damping coefficient. Every mode then decays with `|z| ≈ √(1−σ)` per sample, which gives the closed form

```
σ = 13.8156 / (fs · T60)
```

so any requested decay time `T60` maps to one exact coefficient (clamped at 5 %).

**→ You hear:** the **Decay** knob is literally the T60 of the surface, from 20 ms thuds to 30 s gong-like swells.

### 3. Topology ↔ timbre (the Klein bottle twist)

The grid is a **quotient space**; each axis closes in one of three ways:

| mode      | identification                                 |
|-----------|------------------------------------------------|
| `periodic`| `(x, y) ~ (x + nx, y)`                        |
| `flip`    | `(x, y) ~ (x, y + ny)` with `x → nx−1−x`       |
| `clamp`   | reflecting edge (free boundary)                |

The Klein bottle is **periodic in x + flipped in y**: a wave that circles the twisted direction comes back *travelling the other way, phase-mirrored*. The surface can never consistently have an "inside". The **Topology** switch re-identifies all four voice meshes live — Klein bottle, Möbius band or Membrane — and the 3-D view re-draws itself.

**→ You hear:** the same excitation produces three strongly different instruments: an asymmetric, wide Klein field; an anti-phase, twisted Möbius spectrum; or a dark, centred membrane response.

### 4. Note ↔ pitch ↔ grid size

The fundamental of the longest axis is `f₀ ≈ fs / (√2 · nx)`. A note-on therefore **rebuilds the mesh at the resolution that makes the surface itself resonate at that pitch** — the tuning is the geometry, never a detuned playback. For low notes the required `nx` would be huge, so the voice runs the mesh at `fold · fs` (an octave or more up) and box-averages the output down: exact pitch, bounded CPU. A per-note **node budget** keeps the worst case affordable.

**→ You hear:** every note is a *different physical object* of the same family; timbre drifts across the keyboard the way real instruments do. A subtle tuned fundamental keeps the played pitch identifiable even when the topology produces stronger inharmonic modes.


### 5. Exciters ↔ playing technique

| Exciter      | Math injected into the mesh                                            | Acoustic analogy |
|--------------|------------------------------------------------------------------------|------------------|
| **Mallet**   | velocity impulse (raised-cosine patch, radius 1) on `u[n]`             | struck bar / gong |
| **Pluck**    | displacement bump on `u[n]`                                            | picked membrane |
| **Noise Burst** | spatial noise patch, radius grows as *Hardness* falls              | soft mallet on a drum |
| **Bow**      | per-sample **negative-resistance pump** `f = p·tanh(κ·(v_bow − |v_node|))·sign` | bowed string |
| **Wind**     | two-pole low-pass **breath jet**: filtered noise `− 0.15·u` feedback at the node | flute / recorder |

The bow term brakes the node when it moves faster than the bow and pushes it when it is slower — a self-oscillating limit cycle at node velocity `≈ v_bow`. **→ You hear:** violin-like sustain that goes on for as long as the key is held. The wind jet does the same for air columns: it sustains while the note is held, with **Force** acting as breath pressure. Bow/Wind voices also get ~3× longer damping, so their release tails sing like a violin or flute.

### 6. Where you touch it matters

Exciter and pickup positions `(U, V)` live on the surface. Each mode has a spatial shape, so exciting at a mode's null suppresses it — the same physics as the pickup position of an electric guitar. The **Pickup** reads `u` (displacement, rounder) or `∂u/∂t` (velocity, brighter); two pickups with **Spread** decorrelate the channels into real stereo width. **Motion** makes the exciter orbit the surface — thanks to the twist it periodically "comes back inverted", and you hear the timbre slowly collapse and re-bloom. **Key→Pos U** tracks the exciter with the note for a consistent tone. The mod wheel (CC1) shifts the exciter live.

### 7. Output stage

Per-voice register compensation → voice make-up gain → smoothed output level → `tanh` soft saturation as a safety limiter: loud by design, never brittle.

---

## Features

- **Polyphonic** (4 voices × independent mesh), full MIDI input
- Live **Topology** morphing: Klein / Möbius / Membrane — view and sound reshape together, no note needed
- Exciters: Mallet / Pluck / Noise Burst / Bow / **Wind**
- Streamlined performance controls: Topology, Exciter, Decay, Tone, Motion, Width and Level
- Eight factory presets spanning struck, plucked, bowed, blown and noise-driven sounds
- Topology-specific excitation and pickup models for strongly differentiated timbres
- Tuned fundamental reinforcement for clear MIDI pitch tracking across the keyboard
- Animated **3-D visualisation** of the actual immersion, colour-mapped by the real vibration field (drag = rotate, wheel = zoom)
- **Note ribbon**: drag horizontally to play (re-triggers per semitone), vertical position = velocity
- Sustain pedal (CC64), mod wheel (CC1), Pluck / Panic buttons
- Headless **DSP test suite** (topology tables, stability, T60 accuracy, pitch mapping, bow & wind sustain)

### Factory presets

- Deep Klein Gong
- Fractured Klein
- Mobius Bell
- Bowed Glass
- Breathing Vessel
- Dust Membrane
- Mobius Motion
- Short Wood

## Building from source

Requirements: CMake ≥ 3.22, a C++20 toolchain, and a copy of **JUCE 8** (tested with 8.0.12).

**Windows (Visual Studio 2022/2026):**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DJUCE_PATH="C:/JUCE"
cmake --build build --config Release --parallel
# or simply:  .\scripts\build.ps1
```

**macOS (universal binary):**

```bash
cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DJUCE_PATH="$HOME/JUCE"
cmake --build build --config Release --parallel
```

**Artifacts:**

- VST3: `build/KleinBottle_artefacts/Release/VST3/Klein Bottle Experimental.vst3`
- Standalone: `build/KleinBottle_artefacts/Release/Standalone/Klein Bottle Experimental(.exe|.app)`
- Tests: `build/Release/KleinMeshTests` (headless DSP validation)
- Bench: `build/Release/KleinMeshBench` (per-voice CPU measurement)

To install the VST3 in your DAW, copy it to `C:\Program Files\Common Files\VST3\` (Windows) or `/Library/Audio/Plug-Ins/VST3/` (macOS).

## Releases

Tagged releases (`v*`) are built automatically by GitHub Actions for **Windows x64** and **macOS**; the workflow runs the DSP test suite and attaches a zip per platform (VST3 + Standalone) to the GitHub Release.

## CPU notes

Cost is `nodes × fold × fs`. Low notes automatically use **octave fold-down** and the **node budget is normalised to the host sample rate**, so the per-voice cost stays roughly constant in 44.1 / 48 / 96 / 192 kHz sessions (very high rates trade a little timbre density for bounded CPU). The mesh sweep is hand-vectorised with AVX2 on Windows x64 (portable scalar fallback elsewhere) — measured per-voice cost, % of one core for a continuously ringing note @48 kHz:

| Quality  | C2  | C3  | C4  | A4  |
|----------|-----|-----|-----|-----|
| Eco      | 16% | 17% | 18% | 10% |
| Standard | 44% | 35% | 18% | 14% |
| High     | 77% | 46% | 23% | 14% |

Run the headless `KleinMeshBench` tool to measure your own machine (it also prints the per-sample-rate scaling table).

## License

Copyright © 2026 CVA Labs.

This program is free software: you can redistribute it and/or modify it under the terms of the **GNU Affero General Public License** as published by the Free Software Foundation, either version 3 of the License or (at your option) any later version — see [LICENSE](LICENSE).

It uses the **JUCE framework**, which is dual-licensed (AGPLv3 / commercial). This project is released under the AGPLv3 and does not hold a commercial JUCE license; the VST3 SDK is used via JUCE under Steinberg's GPLv3 dual license. Commercial licensing of the plugin itself is available on request — contact CVA Labs.
