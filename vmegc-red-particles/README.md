# particles

A vmeg sample: 16 round particles drawn into the frame buffer of the PSP by the VME.

## Building

```
cd vmegc-particles
mkdir build && cd build
cmake ..
make
```

## Running

The screen shows the 16 particles, moving and coming back by the other side. HOME exits.

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
| 5 | `renderRow0` to `renderRow15` | 136 each | draw two rows for one particle |

- The kernel `renderRowK` of the particle `K` loads one word for each row (its `dy²`) in `base0` and `base2`, and the two rows are computed in two chains of two pes: `brightness = clamp(profile - dy², 0, 255)` in `pe0` and `pe2`, then the sum with the rows already drawn in `pe1` and `pe3`, whose results stay in `base1` and `base3`. `renderRow0` writes them, `renderRow1` to `renderRow15` add to them. `renderRow15` also stores the two rows in the frame buffer.
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

## Timing

Measured on the PSP with the counter `meCounter`, over 5 seconds: 20.6 ms per frame, 48.6 frames per second computed. The screen shows 60 frames per second at most.

Costs measured on the PSP for the operations of the program:

| Operation | Cost |
|---|---|
| a call of a kernel, with no transfer and 4 elements | 0.002 ms |
| the chain of a row on 2048 elements | 0.014 ms, 6 ns for an element |
| load of a word from the RAM with the DMAC | 102 ns |
| store of a word to the RAM with the DMAC | 8.3 ns |
