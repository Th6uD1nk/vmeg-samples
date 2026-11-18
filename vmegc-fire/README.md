# vmegc-fire

Real-time procedural fire simulation proceded by the VME2 hosted by PSP Media Engine, rendered through the GE in `GU_PSM_T32` CLUT palette mode.

## How it works

1. **`fireStep` (`pipeline` kernel)**: propagates heat upward across 8 bands of `8 x 128` cells (`1024` words per band) with 4-neighbor averaging and cooling (`clamp((total * 61 >> 8) - 1, 0, 255)`).
2. **`fireSeed` (`extended` kernel)**: updates the bottom-row xorshift PRNG (`seedState`) and multiplies by the precomputed centered flame envelope (`fireEnvelopes[envPhase]`).
3. **GE display (`firePaint`)**: binds the VME output buffer directly as a `128 x 64` `GU_PSM_T32` texture with a 256-entry RGBA8888 CLUT (`firePal`) and draws a scaled `GU_SPRITES` quad (`384 x 192`) with `GU_LINEAR` filtering and `GU_CLAMP`.

## Controls

1. `HOME`: exit the sample.

## Build

```bash
cmake -B build -S .
cmake --build build -j4
```
