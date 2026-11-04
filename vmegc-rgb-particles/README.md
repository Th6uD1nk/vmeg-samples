# particles (red, green and blue)

A vmeg sample: 16 round particles drawn into the frame buffer of the PSP by the VME, 5 red, 5 green and 6 blue.

## Building

```
cd vmegc-rgb-particles
mkdir build && cd build
cmake ..
make
```

## Running

The screen shows the 16 particles, moving and coming back by the other side. HOME exits. The particles 0 to 4 are red, 5 to 9 green and 10 to 15 blue. The blue is at half intensity (its byte goes up to 127).

## Program

- The frame buffer is the array `frame`, 512 words per row and 272 rows, one word per pixel (8888, `0x00BBGGRR`, the VME writes 24 bits). The display is configured on SC side using `sceDisplaySetFrameBuf`.
- The state of the particles is four vectors of 16 words, the positions `x` and `y` and the velocities, in half pixels.
- The Media Engine loop does the same steps at each frame, and only calls kernels:

| Step | Kernel | Calls | What the VME does |
|---|---|---|---|
| 1 | `move` | 2 | adds the velocities to the positions of the 16 particles and brings the result back into the screen, 960 half pixels in `x` and 544 in `y` |
| 2 | `profileX` | 16 | for one particle, the 512 words `clamp(255 - dx², 0, 255)` multiplied by the factor of its colour, one for each column |
| 3 | `makeDy` | 16 | for one particle, the 272 words `(2y - cy)²` limited and multiplied by the factor of its colour, one for each row |
| 4 | `loadResident` | 1 | loads the profiles of the 16 particles in `top0` to `top3` (4 particles, 2048 words, in each) |
| 5 | `renderFirst` | 136 | draws two rows for the first particle |
| 6 | `renderMiddle` | 14 × 136 | adds two rows for each one of the next 14 particles |
| 7 | `renderLast` | 136 | adds two rows for the last particle and stores them in the frame buffer |

- `move` is written once with `.with axis in 0..1;` and `.as w[axis];`, and called for `x` and for `y`. The position `p` becomes `p - M * (n + m + 1)` with `n = p >> 23` (-1 for a negative position, 0 otherwise) and `m = (p - M) >> 23` (-1 below `M`, 0 above), the shifts being the `k` of an addition. The sum is read again in `top2` and `top3` with an Agu of offset -9 (the depth of the chain, three cycles per operation) and 25 words, so that it arrives with the product `M * (n + m)`.
- The colour of a particle is the factor of its two tables: 1 for red, 256 for green and 65536 for blue, so that its brightness lands in the byte of its colour. For the blue, the profile and the `dy²` are divided by 2 before the factor (the shift `>> 1` of the subtraction and of the product), so that the brightness goes up to 127 and the disc has the same size as the others, because 255 × 65536 does not fit in the 24 bits signed of the `clamp`.
- `profileX` and `makeDy` are written once with `.with color in 0..2;` and `.as c[color];`: each one makes 3 variants, the calls name the variant with `.as c[n];`. The particles 0 to 4 use `c[0]`, 5 to 9 `c[1]` and 10 to 15 `c[2]`.
- `renderMiddle` is written once with `.with buffer in 0..3, slot in 0..3;` and `.as row[buffer][slot];`: the 14 variants `renderMiddle_0_1` to `renderMiddle_3_2` are called, one for each particle after the first and before the last.
- The kernels of step 5 to 7 load one word for each row (its `dy²`) in `base0` and `base2`, and the two rows are computed in two chains of two pes: `brightness = clamp(profile - dy², 0, 8323072)` in `pe0` and `pe2`, then the sum with the rows already drawn in `pe1` and `pe3`, whose results stay in `base1` and `base3`. The limit 8323072 (`127 << 16`) is the same for the three colours: the tables of red and green never reach it.
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
