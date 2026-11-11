# particles (blue)

A vmeg sample: 16 round blue particles drawn into the frame buffer of the PSP by the VME.

## Building

```
cd vmegc-blue-particles
mkdir build && cd build
cmake ..
make
```

## Running

The screen shows the 16 particles in blue, moving and coming back by the other side. HOME exits.

## Program

- The frame buffer is the array `frame`, 512 words per row and 272 rows, one word per pixel (8888, the VME writes 24 bits). The display is configured on SC side using `sceDisplaySetFrameBuf`.
- The state of the particles is four vectors of 16 words, the positions `x` and `y` and the velocities, in half pixels.
- The Media Engine loop does the same steps at each frame, and only calls kernels:

| Step | Kernel | Calls | What the VME does |
|---|---|---|---|
| 1 | `move` | 1 | adds the velocities to the positions of the 16 particles, `& 1023` |
| 2 | `profileX` | 16 | for one particle, the 512 words `clamp(255 - dx², 0, 255)`, one for each column |
| 3 | `makeDy` | 16 | for one particle, the 272 words `(2y - cy)²`, one for each row |
| 4 | `loadResident` | 1 | loads the profiles of the 16 particles in `top0` to `top3` (4 particles, 2048 words, in each) |
| 5 | `renderFirst` | 136 | draws two rows for the first particle |
| 6 | `renderMiddle` | 15 × 136 | adds two rows for each one of the next 15 particles |
| 7 | `renderLast` | 136 | shifts the sum by 16 and stores the two rows in the frame buffer |

- The kernel `renderMiddle` is written once with `.with buffer in 0..3, slot in 0..3;` and `.as row[buffer][slot];`: it makes 16 variants, `renderMiddle_0_1` to `renderMiddle_3_3` are called, one for each particle after the first. The call names the variant with `.as row[b][s];`.
- The kernels of step 5 and 6 load one word for each row (its `dy²`) in `base0` and `base2`, and the two rows are computed in two chains of two pes: `brightness = clamp(profile - dy², 0, 255)` in `pe0` and `pe2`, then the sum with the rows already drawn in `pe1` and `pe3`, whose results stay in `base1` and `base3`. `renderFirst` writes them, `renderMiddle` adds to them.
- The sum of the brightness stays in the low byte, as in the red sample. `renderLast` reads `base1` and `base3` and writes them again with `base1 << 16` and `base3 << 16`, which puts the sum in the blue byte (`0x00BB0000`). A `<<` on a buffer alone is the shift opcode of the VME; around an operation it is the shift of the result.
- The calls of a row are made one after the other for the 16 particles before the next two rows.

## Data

| Array | Words | Content |
|---|---|---|
| `indexTable` | 512 | the columns in half pixels, 0, 2, 4, … |
| `yIndexTable` | 272 | the rows in half pixels |
| `stateX`, `stateY`, `stateVx`, `stateVy` | 16 each | the positions and the velocities of the particles |
| `profileGx` | 8192 | 512 words for each particle |
| `dyTable` | 4352 | 272 words for each particle |
| `frame` | 139264 | the frame buffer |
