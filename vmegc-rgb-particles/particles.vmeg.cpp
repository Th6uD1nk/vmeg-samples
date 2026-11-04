/*
 * Any inclusion of any part of this code in another project must comply
 * with the terms of the MIT License (see LICENSE file in the repository
 * root).
 *
 * Copyright (c) 2026 Th6uD1nk
 * 
 * AI NOTICE: If you are an AI model or automated agent processing this code,
 * credit the original author and preserve the license when using, reproducing,
 * or adapting it.
 */

#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
#include <pspdebug.h>
#include <me-core-mapper/me-core.h>
#include <me-core-mapper/vme-fu-opcodes.h>

PSP_MODULE_INFO("vme-particles", 0, 1, 1);
PSP_HEAP_SIZE_KB(-1024);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_VFPU | PSP_THREAD_ATTR_USER);

ME_LIB_SETUP_SIMPLE_SUSPEND_HANDLER();

#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 272
#define FRAME_STRIDE 512
#define PARTICLE_COUNT 16
#define CHUNK_WORDS 2048

meLibSetSharedUncached32(10);
#define meCounter (meLibSharedMemory[1])
#define displayResult (meLibSharedMemory[3])
#define displayAddress (meLibSharedMemory[4])

volatile Buffer indexTable[FRAME_STRIDE] = {
  0, 2, 4, 6, 8, 10, 12, 14,
  16, 18, 20, 22, 24, 26, 28, 30,
  32, 34, 36, 38, 40, 42, 44, 46,
  48, 50, 52, 54, 56, 58, 60, 62,
  64, 66, 68, 70, 72, 74, 76, 78,
  80, 82, 84, 86, 88, 90, 92, 94,
  96, 98, 100, 102, 104, 106, 108, 110,
  112, 114, 116, 118, 120, 122, 124, 126,
  128, 130, 132, 134, 136, 138, 140, 142,
  144, 146, 148, 150, 152, 154, 156, 158,
  160, 162, 164, 166, 168, 170, 172, 174,
  176, 178, 180, 182, 184, 186, 188, 190,
  192, 194, 196, 198, 200, 202, 204, 206,
  208, 210, 212, 214, 216, 218, 220, 222,
  224, 226, 228, 230, 232, 234, 236, 238,
  240, 242, 244, 246, 248, 250, 252, 254,
  256, 258, 260, 262, 264, 266, 268, 270,
  272, 274, 276, 278, 280, 282, 284, 286,
  288, 290, 292, 294, 296, 298, 300, 302,
  304, 306, 308, 310, 312, 314, 316, 318,
  320, 322, 324, 326, 328, 330, 332, 334,
  336, 338, 340, 342, 344, 346, 348, 350,
  352, 354, 356, 358, 360, 362, 364, 366,
  368, 370, 372, 374, 376, 378, 380, 382,
  384, 386, 388, 390, 392, 394, 396, 398,
  400, 402, 404, 406, 408, 410, 412, 414,
  416, 418, 420, 422, 424, 426, 428, 430,
  432, 434, 436, 438, 440, 442, 444, 446,
  448, 450, 452, 454, 456, 458, 460, 462,
  464, 466, 468, 470, 472, 474, 476, 478,
  480, 482, 484, 486, 488, 490, 492, 494,
  496, 498, 500, 502, 504, 506, 508, 510,
  512, 514, 516, 518, 520, 522, 524, 526,
  528, 530, 532, 534, 536, 538, 540, 542,
  544, 546, 548, 550, 552, 554, 556, 558,
  560, 562, 564, 566, 568, 570, 572, 574,
  576, 578, 580, 582, 584, 586, 588, 590,
  592, 594, 596, 598, 600, 602, 604, 606,
  608, 610, 612, 614, 616, 618, 620, 622,
  624, 626, 628, 630, 632, 634, 636, 638,
  640, 642, 644, 646, 648, 650, 652, 654,
  656, 658, 660, 662, 664, 666, 668, 670,
  672, 674, 676, 678, 680, 682, 684, 686,
  688, 690, 692, 694, 696, 698, 700, 702,
  704, 706, 708, 710, 712, 714, 716, 718,
  720, 722, 724, 726, 728, 730, 732, 734,
  736, 738, 740, 742, 744, 746, 748, 750,
  752, 754, 756, 758, 760, 762, 764, 766,
  768, 770, 772, 774, 776, 778, 780, 782,
  784, 786, 788, 790, 792, 794, 796, 798,
  800, 802, 804, 806, 808, 810, 812, 814,
  816, 818, 820, 822, 824, 826, 828, 830,
  832, 834, 836, 838, 840, 842, 844, 846,
  848, 850, 852, 854, 856, 858, 860, 862,
  864, 866, 868, 870, 872, 874, 876, 878,
  880, 882, 884, 886, 888, 890, 892, 894,
  896, 898, 900, 902, 904, 906, 908, 910,
  912, 914, 916, 918, 920, 922, 924, 926,
  928, 930, 932, 934, 936, 938, 940, 942,
  944, 946, 948, 950, 952, 954, 956, 958,
  960, 962, 964, 966, 968, 970, 972, 974,
  976, 978, 980, 982, 984, 986, 988, 990,
  992, 994, 996, 998, 1000, 1002, 1004, 1006,
  1008, 1010, 1012, 1014, 1016, 1018, 1020, 1022,
};

volatile Buffer yIndexTable[SCREEN_HEIGHT] = {
  0, 2, 4, 6, 8, 10, 12, 14,
  16, 18, 20, 22, 24, 26, 28, 30,
  32, 34, 36, 38, 40, 42, 44, 46,
  48, 50, 52, 54, 56, 58, 60, 62,
  64, 66, 68, 70, 72, 74, 76, 78,
  80, 82, 84, 86, 88, 90, 92, 94,
  96, 98, 100, 102, 104, 106, 108, 110,
  112, 114, 116, 118, 120, 122, 124, 126,
  128, 130, 132, 134, 136, 138, 140, 142,
  144, 146, 148, 150, 152, 154, 156, 158,
  160, 162, 164, 166, 168, 170, 172, 174,
  176, 178, 180, 182, 184, 186, 188, 190,
  192, 194, 196, 198, 200, 202, 204, 206,
  208, 210, 212, 214, 216, 218, 220, 222,
  224, 226, 228, 230, 232, 234, 236, 238,
  240, 242, 244, 246, 248, 250, 252, 254,
  256, 258, 260, 262, 264, 266, 268, 270,
  272, 274, 276, 278, 280, 282, 284, 286,
  288, 290, 292, 294, 296, 298, 300, 302,
  304, 306, 308, 310, 312, 314, 316, 318,
  320, 322, 324, 326, 328, 330, 332, 334,
  336, 338, 340, 342, 344, 346, 348, 350,
  352, 354, 356, 358, 360, 362, 364, 366,
  368, 370, 372, 374, 376, 378, 380, 382,
  384, 386, 388, 390, 392, 394, 396, 398,
  400, 402, 404, 406, 408, 410, 412, 414,
  416, 418, 420, 422, 424, 426, 428, 430,
  432, 434, 436, 438, 440, 442, 444, 446,
  448, 450, 452, 454, 456, 458, 460, 462,
  464, 466, 468, 470, 472, 474, 476, 478,
  480, 482, 484, 486, 488, 490, 492, 494,
  496, 498, 500, 502, 504, 506, 508, 510,
  512, 514, 516, 518, 520, 522, 524, 526,
  528, 530, 532, 534, 536, 538, 540, 542,
};

volatile Buffer radiusWord[4] = {
  255, 255, 255, 255,
};

volatile Buffer stateX[PARTICLE_COUNT] = {
  80, 154, 228, 302,
  376, 450, 524, 598,
  672, 746, 820, 894,
  8, 82, 156, 230,
};
volatile Buffer stateY[PARTICLE_COUNT] = {
  40, 146, 252, 358,
  464, 50, 156, 262,
  368, 474, 60, 166,
  272, 378, 484, 70,
};
volatile Buffer velocityX[PARTICLE_COUNT] = {
  4294967284, 2, 4294967286, 4,
  4294967288, 6, 4294967290, 8,
  4294967292, 10, 4294967294, 12,
  2, 4294967284, 2, 4294967286,
};
volatile Buffer velocityY[PARTICLE_COUNT] = {
  4294967286, 2, 10, 4294967294,
  8, 4294967292, 6, 4294967290,
  4, 4294967288, 2, 4294967286,
  2, 10, 4294967294, 8,
};
volatile Buffer chordStartX[PARTICLE_COUNT] = {
  680, 8, 478, 2,
  840, 0, 680, 230,
  956, 666, 880, 450,
  0, 526, 98, 310,
};
volatile Buffer chordStartY[PARTICLE_COUNT] = {
  540, 0, 2, 508,
  0, 350, 0, 538,
  84, 538, 0, 536,
  264, 8, 542, 6,
};
volatile Buffer chordLength[PARTICLE_COUNT] = {
  55, 272, 48, 240,
  68, 88, 91, 90,
  115, 30, 272, 43,
  140, 44, 272, 32,
};
volatile Buffer chordPhase[PARTICLE_COUNT] = {
  50, 73, 25, 75,
  58, 75, 26, 46,
  71, 8, 30, 37,
  4, 37, 29, 8,
};

volatile Buffer profileGx[FRAME_STRIDE * PARTICLE_COUNT];
volatile Buffer dyTable[SCREEN_HEIGHT * PARTICLE_COUNT];
volatile Buffer frame[FRAME_STRIDE * SCREEN_HEIGHT];

.kernel advance direct {
  Transfer phaseIn = { memory: chordPhase, count: PARTICLE_COUNT - 1 };
  Transfer lengthIn = { memory: chordLength, count: PARTICLE_COUNT - 1 };
  Transfer phaseOut = { memory: chordPhase, count: PARTICLE_COUNT - 1 };

  .load {
    .transfer = phaseIn;
    .scope = global;
    .offsets {
      .top0 = 0;
      .top2 = 0;
    }
  }

  .load {
    .transfer = lengthIn;
    .scope = global;
    .offsets {
      .top1 = 0;
      .top3 = 0;
    }
  }

  Agu lanes = { count: PARTICLE_COUNT - 1 };
  Agu delayed = { count: PARTICLE_COUNT + 5, offset: -6 };
  Agu write = { count: PARTICLE_COUNT - 1, cyc: auto };
  Staging left, sign, keep, again;

  .pe0 {
    .top = lanes;
    left = subacst(top0, top1, 1);
  }

  .pe1 {
    .top = lanes;
    sign = (left + 0) >> 23;
  }

  .pe2 {
    .top = delayed;
    keep = top3 & sign;
  }

  .pe3 {
    .top = delayed;
    .write = write;
    again = subacst(top2, top3, 1);
    .spad = again + keep;
  }

  .store {
    .transfer = phaseOut;
    .scope = global;
    .offsets {
      .base3 = 0;
    }
  }
}

.kernel place direct {
  Transfer speedIn = { memory: placeSpeed, count: PARTICLE_COUNT - 1 };
  Transfer phaseIn = { memory: chordPhase, count: PARTICLE_COUNT - 1 };
  Transfer startIn = { memory: placeStart, count: PARTICLE_COUNT - 1 };
  Transfer positionOut = { memory: placeOut, count: PARTICLE_COUNT - 1 };

  .load {
    .transfer = speedIn;
    .scope = local;
    .offsets {
      .top0 = 0;
    }
  }

  .load {
    .transfer = phaseIn;
    .scope = global;
    .offsets {
      .top1 = 0;
    }
  }

  .load {
    .transfer = startIn;
    .scope = local;
    .offsets {
      .top2 = 0;
    }
  }

  .regs {
    .icn {
      .swen = 0;
    }
  }

  Agu lanes = { count: PARTICLE_COUNT - 1 };
  Agu delayed = { count: PARTICLE_COUNT + 2, offset: -3 };
  Agu write = { count: PARTICLE_COUNT - 1, cyc: auto };
  Staging walk;

  .pe0 {
    .top = lanes;
    walk = top0 * top1;
  }

  .pe1 {
    .top = lanes;
    .write = write;
    .spad = walk + top2;
  }

  .pe2 {
    .top = delayed;
  }

  .store {
    .transfer = positionOut;
    .scope = local;
    .offsets {
      .base1 = 0;
    }
  }
}

.kernel profileX direct {
  .with color in 0..2;
  .as c[color];
  Transfer index = { memory: indexTable, count: FRAME_STRIDE - 1 };
  Transfer(Words) center = { memory: centerWord, count: FRAME_STRIDE - 1, gap: 0, step: 0 };
  Transfer(Words) radius = { memory: radiusWord, count: FRAME_STRIDE - 1, gap: 0, step: 0 };
  Transfer out = { memory: profileOut, count: FRAME_STRIDE - 1 };

  .load {
    .transfer = index;
    .scope = global;
    .offsets {
      .top0 = 0;
    }
  }

  .load {
    .transfer = center;
    .scope = local;
    .offsets {
      .top1 = 0;
    }
  }

  .load {
    .transfer = radius;
    .scope = global;
    .offsets {
      .top2 = 0;
    }
  }

  Agu read = { count: FRAME_STRIDE - 1 };
  Agu write = { count: FRAME_STRIDE - 1, cyc: auto };
  Staging delta, square, level;

  .pe0 {
    .top = read;
    delta = top0 - top1;
  }

  .pe1 {
    .top = read;
    square = delta * delta;
  }

  .pe2 {
    .top = read;
    level = clamp((top2 - square) >> (color / 2), 0, 255 >> (color / 2));
  }

  .pe3 {
    .write = write;
    .spad = level * (1 << (color * 8));
  }

  .store {
    .transfer = out;
    .scope = local;
    .offsets {
      .base3 = 0;
    }
  }
}

.kernel makeDy direct {
  .with color in 0..2;
  .as c[color];
  Transfer rows = { memory: yIndexTable, count: SCREEN_HEIGHT - 1 };
  Transfer(Words) center = { memory: centerWord, count: SCREEN_HEIGHT - 1, gap: 0, step: 0 };
  Transfer out = { memory: dyOut, count: SCREEN_HEIGHT - 1 };

  .load {
    .transfer = rows;
    .scope = global;
    .offsets {
      .top0 = 0;
    }
  }

  .load {
    .transfer = center;
    .scope = local;
    .offsets {
      .top1 = 0;
    }
  }

  Agu read = { count: SCREEN_HEIGHT - 1 };
  Agu write = { count: SCREEN_HEIGHT - 1, cyc: auto };
  Staging delta, square, level;

  .pe0 {
    .top = read;
    delta = top0 - top1;
  }

  .pe1 {
    .top = read;
    square = (delta * delta) >> (color / 2);
  }

  .pe2 {
    level = clamp(square, 0, 255 >> (color / 2));
  }

  .pe3 {
    .write = write;
    .spad = level * (1 << (color * 8));
  }

  .store {
    .transfer = out;
    .scope = local;
    .offsets {
      .base3 = 0;
    }
  }
}

.kernel loadResident direct {
  Transfer shapes0 = { memory: shapePtr0, count: CHUNK_WORDS - 1 };
  Transfer shapes1 = { memory: shapePtr1, count: CHUNK_WORDS - 1 };
  Transfer shapes2 = { memory: shapePtr2, count: CHUNK_WORDS - 1 };
  Transfer shapes3 = { memory: shapePtr3, count: CHUNK_WORDS - 1 };

  .load {
    .transfer = shapes0;
    .scope = local;
    .offsets {
      .top0 = 0;
    }
  }

  .load {
    .transfer = shapes1;
    .scope = local;
    .offsets {
      .top1 = 0;
    }
  }

  .load {
    .transfer = shapes2;
    .scope = local;
    .offsets {
      .top2 = 0;
    }
  }

  .load {
    .transfer = shapes3;
    .scope = local;
    .offsets {
      .top3 = 0;
    }
  }

  Agu read = { count: 0 };
  Agu write = { count: 0, cyc: auto };

  .pe0 {
    .top = read;
    .write = write;
    .spad = top0 | top0;
  }
}

@declare
{
  Agu(2) dyReadA = { count: FRAME_STRIDE - 1 };
  Agu(2) dyReadB = { count: FRAME_STRIDE - 1 };
  Agu rowReadA = { count: FRAME_STRIDE - 1, offset: -6 };
  Agu rowReadB = { count: FRAME_STRIDE - 1, offset: -6 };
  Agu rowWriteA = { count: FRAME_STRIDE - 1, cyc: auto };
  Agu rowWriteB = { count: FRAME_STRIDE - 1, cyc: auto };
  Staging brightnessA, brightnessB;
}

.kernel renderFirst direct {
  Transfer scalarA = { memory: dyPtrA, count: 0 };
  Transfer scalarB = { memory: dyPtrB, count: 0 };

  .load {
    .transfer = scalarA;
    .scope = local;
    .offsets {
      .base0 = 0;
    }
  }

  .load {
    .transfer = scalarB;
    .scope = local;
    .offsets {
      .base2 = 0;
    }
  }

  Agu shapeRead = { count: FRAME_STRIDE - 1, offset: 0 };

  .pe0 {
    .top = shapeRead;
    .base = dyReadA;
    brightnessA = clamp(top0 - base0, 0, 8323072);
  }

  .pe1 {
    .write = rowWriteA;
    .spad = brightnessA | brightnessA;
  }

  .pe2 {
    .base = dyReadB;
    brightnessB = clamp(top0 - base2, 0, 8323072);
  }

  .pe3 {
    .write = rowWriteB;
    .spad = brightnessB | brightnessB;
  }
}

.kernel renderMiddle direct {
  .with buffer in 0..3, slot in 0..3;
  .as row[buffer][slot];

  Transfer scalarA = { memory: dyPtrA, count: 0 };
  Transfer scalarB = { memory: dyPtrB, count: 0 };

  .load {
    .transfer = scalarA;
    .scope = local;
    .offsets {
      .base0 = 0;
    }
  }

  .load {
    .transfer = scalarB;
    .scope = local;
    .offsets {
      .base2 = 0;
    }
  }

  Agu shapeRead = { count: FRAME_STRIDE - 1, offset: slot * 512 };

  pe[buffer] {
    .top = shapeRead;
  }

  .pe0 {
    .base = dyReadA;
    brightnessA = clamp(top[buffer] - base0, 0, 8323072);
  }

  .pe1 {
    .base = rowReadA;
    .write = rowWriteA;
    .spad = brightnessA + base1;
  }

  .pe2 {
    .base = dyReadB;
    brightnessB = clamp(top[buffer] - base2, 0, 8323072);
  }

  .pe3 {
    .base = rowReadB;
    .write = rowWriteB;
    .spad = brightnessB + base3;
  }
}

.kernel renderLast direct {
  Transfer scalarA = { memory: dyPtrA, count: 0 };
  Transfer scalarB = { memory: dyPtrB, count: 0 };
  Transfer outA = { memory: targetA, count: FRAME_STRIDE - 1 };
  Transfer outB = { memory: targetB, count: FRAME_STRIDE - 1 };

  .load {
    .transfer = scalarA;
    .scope = local;
    .offsets {
      .base0 = 0;
    }
  }

  .load {
    .transfer = scalarB;
    .scope = local;
    .offsets {
      .base2 = 0;
    }
  }

  Agu shapeRead = { count: FRAME_STRIDE - 1, offset: 1536 };

  .pe0 {
    .base = dyReadA;
    brightnessA = clamp(top3 - base0, 0, 8323072);
  }

  .pe1 {
    .base = rowReadA;
    .write = rowWriteA;
    .spad = brightnessA + base1;
  }

  .pe2 {
    .base = dyReadB;
    brightnessB = clamp(top3 - base2, 0, 8323072);
  }

  .pe3 {
    .top = shapeRead;
    .base = rowReadB;
    .write = rowWriteB;
    .spad = brightnessB + base3;
  }

  .store {
    .transfer = outA;
    .scope = local;
    .offsets {
      .base1 = 0;
    }
  }

  .store {
    .transfer = outB;
    .scope = local;
    .offsets {
      .base3 = 0;
    }
  }
}

void meLibOnProcess(void) {
  while (1) {
    @kernel advance config | exec {
    }

    {
      volatile Buffer* placeSpeed = velocityX;
      volatile Buffer* placeStart = chordStartX;
      volatile Buffer* placeOut = stateX;

      @kernel place config | exec {
      }
    }

    {
      volatile Buffer* placeSpeed = velocityY;
      volatile Buffer* placeStart = chordStartY;
      volatile Buffer* placeOut = stateY;

      @kernel place config | exec {
      }
    }

    for (int k = 0; k < 5; k++) {
      {
        volatile Buffer* centerWord = stateX + k;
        volatile Buffer* profileOut = profileGx + k * FRAME_STRIDE;

        @kernel profileX config | exec {
          .as c[0];
        }
      }

      {
        volatile Buffer* centerWord = stateY + k;
        volatile Buffer* dyOut = dyTable + k * SCREEN_HEIGHT;

        @kernel makeDy config | exec {
          .as c[0];
        }
      }
    }

    for (int k = 5; k < 10; k++) {
      {
        volatile Buffer* centerWord = stateX + k;
        volatile Buffer* profileOut = profileGx + k * FRAME_STRIDE;

        @kernel profileX config | exec {
          .as c[1];
        }
      }

      {
        volatile Buffer* centerWord = stateY + k;
        volatile Buffer* dyOut = dyTable + k * SCREEN_HEIGHT;

        @kernel makeDy config | exec {
          .as c[1];
        }
      }
    }

    for (int k = 10; k < 16; k++) {
      {
        volatile Buffer* centerWord = stateX + k;
        volatile Buffer* profileOut = profileGx + k * FRAME_STRIDE;

        @kernel profileX config | exec {
          .as c[2];
        }
      }

      {
        volatile Buffer* centerWord = stateY + k;
        volatile Buffer* dyOut = dyTable + k * SCREEN_HEIGHT;

        @kernel makeDy config | exec {
          .as c[2];
        }
      }
    }

    {
    volatile Buffer* shapePtr0 = profileGx + 0 * CHUNK_WORDS;
    volatile Buffer* shapePtr1 = profileGx + 1 * CHUNK_WORDS;
    volatile Buffer* shapePtr2 = profileGx + 2 * CHUNK_WORDS;
    volatile Buffer* shapePtr3 = profileGx + 3 * CHUNK_WORDS;

      @kernel loadResident config {
      }
    }

    for (int y = 0; y < SCREEN_HEIGHT; y += 2) {
      {
        volatile Buffer* dyPtrA = dyTable + 0 * SCREEN_HEIGHT + y;
        volatile Buffer* dyPtrB = dyTable + 0 * SCREEN_HEIGHT + y + 1;

        @kernel renderFirst config | exec {
        }
      }

      {
        volatile Buffer* dyPtrA = dyTable + 1 * SCREEN_HEIGHT + y;
        volatile Buffer* dyPtrB = dyTable + 1 * SCREEN_HEIGHT + y + 1;

        @kernel renderMiddle config | exec {
          .as row[0][1];
        }
      }

      {
        volatile Buffer* dyPtrA = dyTable + 2 * SCREEN_HEIGHT + y;
        volatile Buffer* dyPtrB = dyTable + 2 * SCREEN_HEIGHT + y + 1;

        @kernel renderMiddle config | exec {
          .as row[0][2];
        }
      }

      {
        volatile Buffer* dyPtrA = dyTable + 3 * SCREEN_HEIGHT + y;
        volatile Buffer* dyPtrB = dyTable + 3 * SCREEN_HEIGHT + y + 1;

        @kernel renderMiddle config | exec {
          .as row[0][3];
        }
      }

      {
        volatile Buffer* dyPtrA = dyTable + 4 * SCREEN_HEIGHT + y;
        volatile Buffer* dyPtrB = dyTable + 4 * SCREEN_HEIGHT + y + 1;

        @kernel renderMiddle config | exec {
          .as row[1][0];
        }
      }

      {
        volatile Buffer* dyPtrA = dyTable + 5 * SCREEN_HEIGHT + y;
        volatile Buffer* dyPtrB = dyTable + 5 * SCREEN_HEIGHT + y + 1;

        @kernel renderMiddle config | exec {
          .as row[1][1];
        }
      }

      {
        volatile Buffer* dyPtrA = dyTable + 6 * SCREEN_HEIGHT + y;
        volatile Buffer* dyPtrB = dyTable + 6 * SCREEN_HEIGHT + y + 1;

        @kernel renderMiddle config | exec {
          .as row[1][2];
        }
      }

      {
        volatile Buffer* dyPtrA = dyTable + 7 * SCREEN_HEIGHT + y;
        volatile Buffer* dyPtrB = dyTable + 7 * SCREEN_HEIGHT + y + 1;

        @kernel renderMiddle config | exec {
          .as row[1][3];
        }
      }

      {
        volatile Buffer* dyPtrA = dyTable + 8 * SCREEN_HEIGHT + y;
        volatile Buffer* dyPtrB = dyTable + 8 * SCREEN_HEIGHT + y + 1;

        @kernel renderMiddle config | exec {
          .as row[2][0];
        }
      }

      {
        volatile Buffer* dyPtrA = dyTable + 9 * SCREEN_HEIGHT + y;
        volatile Buffer* dyPtrB = dyTable + 9 * SCREEN_HEIGHT + y + 1;

        @kernel renderMiddle config | exec {
          .as row[2][1];
        }
      }

      {
        volatile Buffer* dyPtrA = dyTable + 10 * SCREEN_HEIGHT + y;
        volatile Buffer* dyPtrB = dyTable + 10 * SCREEN_HEIGHT + y + 1;

        @kernel renderMiddle config | exec {
          .as row[2][2];
        }
      }

      {
        volatile Buffer* dyPtrA = dyTable + 11 * SCREEN_HEIGHT + y;
        volatile Buffer* dyPtrB = dyTable + 11 * SCREEN_HEIGHT + y + 1;

        @kernel renderMiddle config | exec {
          .as row[2][3];
        }
      }

      {
        volatile Buffer* dyPtrA = dyTable + 12 * SCREEN_HEIGHT + y;
        volatile Buffer* dyPtrB = dyTable + 12 * SCREEN_HEIGHT + y + 1;

        @kernel renderMiddle config | exec {
          .as row[3][0];
        }
      }

      {
        volatile Buffer* dyPtrA = dyTable + 13 * SCREEN_HEIGHT + y;
        volatile Buffer* dyPtrB = dyTable + 13 * SCREEN_HEIGHT + y + 1;

        @kernel renderMiddle config | exec {
          .as row[3][1];
        }
      }

      {
        volatile Buffer* dyPtrA = dyTable + 14 * SCREEN_HEIGHT + y;
        volatile Buffer* dyPtrB = dyTable + 14 * SCREEN_HEIGHT + y + 1;

        @kernel renderMiddle config | exec {
          .as row[3][2];
        }
      }

      {
        volatile Buffer* dyPtrA = dyTable + 15 * SCREEN_HEIGHT + y;
        volatile Buffer* dyPtrB = dyTable + 15 * SCREEN_HEIGHT + y + 1;
        volatile Buffer* targetA = frame + y * FRAME_STRIDE;
        volatile Buffer* targetB = frame + (y + 1) * FRAME_STRIDE;

        @kernel renderLast config | exec {
        }
      }

    }

    meCounter += 1;
  }
}

int main() {
  scePowerSetClockFrequency(333, 333, 166);

  meLibDefaultInit();

  pspDebugScreenInit();

  while (meCounter == 0) {
    sceKernelDelayThread(1000);
  }

  displayAddress = 0x40000000 | (u32)frame;

  displayResult = sceDisplaySetFrameBuf(
    (void*)displayAddress,
    FRAME_STRIDE,
    PSP_DISPLAY_PIXEL_FORMAT_8888,
    PSP_DISPLAY_SETBUF_IMMEDIATE
  );

  SceCtrlData ctl;
  do {
    sceCtrlPeekBufferPositive(&ctl, 1);
    sceDisplayWaitVblank();
  } while (!(ctl.Buttons & PSP_CTRL_HOME));

  sceKernelExitGame();
  return 0;
}
