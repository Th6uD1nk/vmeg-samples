#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <psppower.h>
#include <me-core-mapper/me-core.h>
#include <me-core-mapper/vme-fu-opcodes.h>
PSP_MODULE_INFO("vme-fire", 0, 1, 1);
PSP_HEAP_SIZE_KB(-1024);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_VFPU | PSP_THREAD_ATTR_USER);
ME_LIB_SETUP_SIMPLE_SUSPEND_HANDLER();
#define FIRE_ROW_CELLS 128
#define FIRE_HEIGHT 64
#define FIRE_ROWS 63
#define FIRE_FIELD (FIRE_ROW_CELLS * (FIRE_HEIGHT + 1))
#define FIRE_BAND_ROWS 8
#define FIRE_BAND_CELLS (FIRE_ROW_CELLS * FIRE_BAND_ROWS)
#define FIRE_SCALE 3
#define FIRE_VIEW_W (FIRE_ROW_CELLS * FIRE_SCALE)
#define FIRE_VIEW_H (FIRE_HEIGHT * FIRE_SCALE)
#define FIRE_VIEW_X ((480 - FIRE_VIEW_W) / 2)
#define FIRE_VIEW_Y ((272 - FIRE_VIEW_H) / 2)
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 272
#define FRAME_STRIDE 512

meLibSetSharedUncached32(10);
#define fireRequest (meLibSharedMemory[1])
#define fireDone (meLibSharedMemory[2])

volatile Buffer heatA[FIRE_FIELD];
volatile Buffer heatB[FIRE_FIELD];
volatile Buffer seedState[FIRE_ROW_CELLS];
volatile Buffer fireEnvelopes[120][FIRE_ROW_CELLS];
static u32 firePal[256] __attribute__((aligned(64)));

static u32 __attribute__((aligned(16))) guList[256];

static u32 fireColor(int r, int g, int b) { return 0xff000000 | ((u32)b << 16) | ((u32)g << 8) | (u32)r; }
static void firePaletteInit(void) {
  static const int stop[6] = { 0, 48, 112, 176, 224, 255 };
  static const u32 color[6] = { 0xff000000, 0xff000050, 0xff0000ff, 0xff00a0ff, 0xff50ffff, 0xffffffff };
  for (int i = 0; i < 256; i++) {
    int s = 0;
    while (s < 4 && i > stop[s + 1]) s += 1;
    int span = stop[s + 1] - stop[s], t = span > 0 ? ((i - stop[s]) * 256) / span : 0;
    u32 a = color[s], b = color[s + 1];
    firePal[i] = fireColor((((a & 0xff) * (256 - t)) + ((b & 0xff) * t)) >> 8, ((((a >> 8) & 0xff) * (256 - t)) + (((b >> 8) & 0xff) * t)) >> 8, ((((a >> 16) & 0xff) * (256 - t)) + (((b >> 16) & 0xff) * t)) >> 8);
  }
}
static void fireEnvelopeInit(void) {
  for (int p = 0; p < 120; p++) {
    int breath = 210 + ((p < 60 ? p : 120 - p) * 45) / 59;
    for (int x = 0; x < FIRE_ROW_CELLS; x++) {
      int d = x * 2 - (FIRE_ROW_CELLS - 1), ad = d < 0 ? -d : d;
      int window = 255 - (ad * ad) / 48, tri = (ad + p / 2) % 22, depth = (ad * 255) / 110;
      if (window < 0) window = 0;
      if (depth > 255) depth = 255;
      int jag = ((tri < 11 ? tri : 22 - tri) * 255) / 11, teeth = 255 - (jag * depth) / 255;
      int v = (((window * breath) >> 8) * teeth) >> 8;
      fireEnvelopes[p][x] = (u32)(v > 255 ? 255 : v);
    }
  }
}
static void fireFieldClear(void) {
  for (int i = 0; i < FIRE_FIELD; i++) heatA[i] = heatB[i] = 0;
  for (int i = 0; i < FIRE_ROW_CELLS; i++) seedState[i] = (u32)(i * 37 + 1) * 2654435761u;
}
static void fireGuInit(void) {
  sceGuInit();
  sceGuStart(GU_DIRECT, guList);
  sceGuDrawBuffer(GU_PSM_8888, (void*)0, FRAME_STRIDE);
  sceGuDispBuffer(SCREEN_WIDTH, SCREEN_HEIGHT, (void*)(FRAME_STRIDE * SCREEN_HEIGHT * 4), FRAME_STRIDE);
  sceGuOffset(2048 - SCREEN_WIDTH / 2, 2048 - SCREEN_HEIGHT / 2);
  sceGuViewport(2048, 2048, SCREEN_WIDTH, SCREEN_HEIGHT);
  sceGuScissor(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  sceGuEnable(GU_SCISSOR_TEST);
  sceGuDisable(GU_DEPTH_TEST);
  sceGuEnable(GU_TEXTURE_2D);
  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
  sceGuTexFilter(GU_LINEAR, GU_LINEAR);
  sceGuTexWrap(GU_CLAMP, GU_CLAMP);
  sceGuFinish();
  sceGuSync(0, 0);
  sceDisplayWaitVblankStart();
  sceGuDisplay(GU_TRUE);
}

static void firePaint(const volatile u32* field) {
  sceGuStart(GU_DIRECT, guList);
  sceGuClearColor(0xff000000);
  sceGuClear(GU_COLOR_BUFFER_BIT);
  sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
  sceGuClutLoad(32, firePal);
  sceGuTexMode(GU_PSM_T32, 0, 0, 0);
  sceGuTexImage(0, 128, FIRE_HEIGHT, 128, (const void*)field);
  typedef struct { u16 u, v; s16 x, y, z; } FireVertex;
  FireVertex* v = (FireVertex*)sceGuGetMemory(2 * sizeof(FireVertex));
  v[0].u = 0; v[0].v = 0; v[0].x = FIRE_VIEW_X; v[0].y = FIRE_VIEW_Y; v[0].z = 0;
  v[1].u = FIRE_ROW_CELLS; v[1].v = FIRE_HEIGHT; v[1].x = FIRE_VIEW_X + FIRE_VIEW_W; v[1].y = FIRE_VIEW_Y + FIRE_VIEW_H; v[1].z = 0;
  sceGuDrawArray(GU_SPRITES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
  sceGuFinish();
  sceGuSync(0, 0);
  sceDisplayWaitVblankStart();
  sceGuSwapBuffers();
}

.kernel fireStep pipeline {
  Transfer leftIn = { memory: leftPtr, count: FIRE_BAND_CELLS - 1 };
  Transfer centerIn = { memory: centerPtr, count: FIRE_BAND_CELLS - 1 };
  Transfer rightIn = { memory: rightPtr, count: FIRE_BAND_CELLS - 1 };
  Transfer farIn = { memory: farPtr, count: FIRE_BAND_CELLS - 1 };
  Transfer outBand = { memory: outPtr, count: FIRE_BAND_CELLS - 1 };

  .load { .transfer = leftIn; .scope = local; .offsets { .top0 = 0; } }
  .load { .transfer = centerIn; .scope = local; .offsets { .top1 = 0; } }
  .load { .transfer = rightIn; .scope = local; .offsets { .top2 = 0; } }
  .load { .transfer = farIn; .scope = local; .offsets { .top3 = 0; } }

  .flow {
    .count = FIRE_BAND_CELLS - 1;
    Slot low = top0 + top1;
    Slot high = top2 + top3;
    Slot total = low + high;
    Slot mul61 = total * 61;
    Slot shifted = mul61 >> 8;
    .slot = clamp(shifted - 1, 0, 255);
  }

  .store { .transfer = outBand; .scope = local; }
}

.kernel fireSeed extended {
  Transfer stateIn = { memory: statePtr, count: FIRE_ROW_CELLS - 1 };
  Transfer envelopeIn = { memory: envelopePtr, count: FIRE_ROW_CELLS - 1 };
  Transfer stateOut = { memory: statePtr, count: FIRE_ROW_CELLS - 1 };
  Transfer rowOut = { memory: rowPtr, count: FIRE_ROW_CELLS - 1 };
  .load { .transfer = stateIn; .scope = local; .offsets { .top0 = 0; } }
  .load { .transfer = envelopeIn; .scope = local; .offsets { .top4 = 0; } }
  Agu lanes = { count: FIRE_ROW_CELLS - 1 };
  Agu writeState = { count: FIRE_ROW_CELLS - 1, cyc: auto };
  Agu writeHeat = { count: FIRE_ROW_CELLS - 1, cyc: auto };
  Staging x1, a, x2, b, x3, c, x4, amp, prod;
  .pe0 { .top = lanes; x1 = top0 << 7; a = top0 ^ x1; }
  .pe1 { x2 = a >> 9; b = a ^ x2; }
  .pe2 { x3 = b << 8; c = b ^ x3; }
  .pe3 { x4 = c & 127; amp = x4 + 160; }
  .pe4 { .top = { count: FIRE_ROW_CELLS - 1, cyc: auto }; prod = amp * top4; }
  .pe5 { .write = writeState; .spad = c; }
  .pe6 { .write = writeHeat; .spad = clamp(prod >> 8, 0, 255); }
  .store { .transfer = stateOut; .scope = local; .offsets { .base5 = 0; } }
  .store { .transfer = rowOut; .scope = local; .offsets { .base6 = 0; } }
}

void meLibOnProcess(void) {
  u32 done = 0;
  int phase = 0;
  int envPhase = 0;
  while (1) {
    while (fireRequest == done) {
    }
    done = fireRequest;
    volatile Buffer* src = phase ? heatB : heatA;
    volatile Buffer* dst = phase ? heatA : heatB;
    for (int band = 0; band < 8; band++) {
      int row = band * FIRE_BAND_ROWS;
      volatile Buffer* base = src + (row + 1) * FIRE_ROW_CELLS;
      volatile Buffer* leftPtr = base - 1;
      volatile Buffer* centerPtr = base;
      volatile Buffer* rightPtr = base + 1;
      volatile Buffer* farPtr = base + FIRE_ROW_CELLS;
      volatile Buffer* outPtr = dst + row * FIRE_ROW_CELLS;
      @kernel fireStep config | exec {
      }
    }
    {
      volatile Buffer* statePtr = seedState;
      volatile Buffer* envelopePtr = fireEnvelopes[envPhase];
      volatile Buffer* rowPtr = dst + FIRE_ROWS * FIRE_ROW_CELLS;
      @kernel fireSeed config | exec {
      }
    }
    fireDone = done;
    phase ^= 1;
    envPhase = (envPhase + 1) % 120;
  }
}

int main() {
  scePowerSetClockFrequency(333, 333, 166);
  fireFieldClear();
  fireEnvelopeInit();
  firePaletteInit();
  sceKernelDcacheWritebackInvalidateAll();
  meLibDefaultInit();
  fireGuInit();
  SceCtrlData ctl;
  u32 request = 1;
  u32 shown = 0;
  int running = 1;
  fireRequest = request;
  while (running) {
    sceCtrlPeekBufferPositive(&ctl, 1);
    if (ctl.Buttons & PSP_CTRL_HOME) {
      running = 0;
    }
    if (shown != fireDone) {
      shown = fireDone;
      const volatile u32* ready = (shown & 1) ? heatB : heatA;
      request += 1;
      fireRequest = request;
      firePaint(ready);
    } else {
      sceDisplayWaitVblankStart();
    }
  }
  sceGuTerm();
  sceKernelExitGame();
  return 0;
}
