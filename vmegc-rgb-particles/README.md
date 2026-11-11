# particles (red, green and blue)

A vmeg sample: 16 round particles drawn into the frame buffer of the PSP by the VME, 5 red, 5 green and 6 blue.

## Building

```
cd vmegc-rgb-particles
mkdir build && cd build
cmake ..
make clean; make;
```

or
```
cmake -DVMEGC_COMPAT=fat ..
make clean; make;
```

## Running

The screen shows the 16 particles, each one moving along its own straight line at its own speed. When a particle leaves the screen, it comes back at the start of the same line and goes along it again, so every particle draws one single line. HOME exits. The particles 0 to 4 are red, 5 to 9 green and 10 to 15 blue. The blue is at half intensity (its byte goes up to 127).

## Program

- The frame buffer is the array `frame`, 512 words per row and 272 rows, one word per pixel (8888, `0x00BBGGRR`, the VME writes 24 bits). The display is configured on SC side using `sceDisplaySetFrameBuf`.
- The state of the particles is the positions `x` and `y`, in half pixels, and the phase of each particle on its line (16 words each). The velocities, the start and the length of the line of each particle are constants, in half pixels.
- The Media Engine loop does the same steps at each frame, and only calls kernels:

| Step | Kernel | Calls | What the VME does |
|---|---|---|---|
| 1 | `advance` | 1 | adds 1 to the phase of the 16 particles and brings it back to 0 at the end of the line |
| 2 | `place` | 2 | for `x` and for `y`, the position is the start of the line plus the velocity times the phase |
| 3 | `profileX` | 16 | for one particle, the 512 words `clamp(255 - dx², 0, 255)` multiplied by the factor of its colour, one for each column |
| 4 | `makeDy` | 16 | for one particle, the 272 words `(2y - cy)²` limited and multiplied by the factor of its colour, one for each row |
| 5 | `loadResident` | 1 | loads the profiles of the 16 particles in `top0` to `top3` (4 particles, 2048 words, in each) |
| 6 | `renderFirst` | 136 | draws two rows for the first particle |
| 7 | `renderMiddle` | 14 × 136 | adds two rows for each one of the next 14 particles |
| 8 | `renderLast` | 136 | adds two rows for the last particle and stores them in the frame buffer |

- Each particle goes along one straight line. When it leaves the screen it comes back at the start of the same line (the point where the line enters the screen) and goes along it again, so it draws one single line. The start, the length and the phase of the line of each particle are arrays of the program, the length being the number of steps during which the centre stays inside the screen (960 by 544 half pixels). `advance` computes the new phase `d = phase + 1 - length` and `phase' = d + (length & (d >> 23))`, the shift being the `k` of an addition, and the length and the phase are read a second time with an Agu of offset -6 to arrive with the result of the chain. `place` does `start + velocity * phase`, the start read with an Agu of offset -3.
- The colour of a particle is the factor of its two tables: 1 for red, 256 for green and 65536 for blue, so that its brightness lands in the byte of its colour. For the blue, the profile and the `dy²` are divided by 2 before the factor (the shift `>> 1` of the subtraction and of the product), so that the brightness goes up to 127 and the disc has the same size as the others, because 255 × 65536 does not fit in the 24 bits signed of the `clamp`.
- `profileX` and `makeDy` are written once with `.with color in 0..2;` and `.as c[color];`: each one makes 3 variants, the calls name the variant with `.as c[n];`. The particles 0 to 4 use `c[0]`, 5 to 9 `c[1]` and 10 to 15 `c[2]`.
- `renderMiddle` is written once with `.with buffer in 0..3, slot in 0..3;` and `.as row[buffer][slot];`: the 14 variants `renderMiddle_0_1` to `renderMiddle_3_2` are called, one for each particle after the first and before the last.
- The kernels of step 6 to 8 load one word for each row (its `dy²`) in `base0` and `base2`, and the two rows are computed in two chains of two pes: `brightness = clamp(profile - dy², 0, 8323072)` in `pe0` and `pe2`, then the sum with the rows already drawn in `pe1` and `pe3`, whose results stay in `base1` and `base3`. The limit 8323072 (`127 << 16`) is the same for the three colours: the tables of red and green never reach it.
- The calls of a row are made one after the other for the 16 particles before the next two rows.

## Data

| Array | Words | Content |
|---|---|---|
| `indexTable` | 512 | the columns in half pixels, 0, 2, 4, … |
| `yIndexTable` | 272 | the rows in half pixels |
| `stateX`, `stateY` | 16 each | the positions of the particles |
| `velocityX`, `velocityY`, `chordStartX`, `chordStartY`, `chordLength`, `chordPhase` | 16 each | the velocity, the start and the length of the line of each particle, and its phase on it |
| `profileGx` | 8192 | 512 words for each particle |
| `dyTable` | 4352 | 272 words for each particle |
| `frame` | 139264 | the frame buffer |
