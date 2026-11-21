# vmegc-fire

Real-time procedural fire simulation processed by the VME2 hosted by PSP Media Engine, rendered in 3D through the GE in `GU_PSM_T32` CLUT palette mode.

## How it works

1. **`fireWave` (`extended` kernel)**: rotates a `Buffer("Q16.8")` harmonic oscillator per band (`8 x 128` cells) and outputs the height-scaled horizontal sway factor (`waveSway`).
2. **`fireStep` (`extended` kernel)**: propagates heat upward across 8 bands of `1024` words with 4-neighbor averaging, lateral advection via `mulclp` from `waveSway`, `(adv * 62) >> 8` cooling, and conditional core pinching (`(cooled & 128) ? cooled + 3 : cooled - 3`).
3. **`fireSeed` (`extended` kernel)**: updates the bottom-row xorshift PRNG (`seedState`) and multiplies by a smoothed dual-offset read (`.top4 = 0`, `.top7 = 2`) of the precomputed flame envelope (`fireEnvelopes[envPhase]`).

## Controls

1. `HOME`: exit the sample.

## Build

```bash
cmake -B build -S .
cmake --build build -j4
```
