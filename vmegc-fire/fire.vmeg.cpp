#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
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
#define FIRE_FIELD (FIRE_ROW_CELLS * (FIRE_HEIGHT + 2))
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

meLibSetSharedUncached32(16);
#define fireRequest (meLibSharedMemory[1])
#define fireDone (meLibSharedMemory[2])

volatile Buffer heatA[FIRE_FIELD];
volatile Buffer heatB[FIRE_FIELD];
volatile Buffer seedState[FIRE_ROW_CELLS];
volatile Buffer fireEnvelopes[120][FIRE_ROW_CELLS];
static u32 firePal[256] __attribute__((aligned(64)));

static u32 __attribute__((aligned(16))) guList[8192];

static u32 fireColor(int a, int r, int g, int b) {
  return ((u32)a << 24) | ((u32)b << 16) | ((u32)g << 8) | (u32)r;
}

static void firePaletteInit(void) {
  static const int stop[6] = { 0, 48, 112, 176, 224, 255 };
  static const u32 color[6] = { 0xff000000, 0xff000050, 0xff0000ff, 0xff00a0ff, 0xff50ffff, 0xffffffff };
  for (int i = 0; i < 256; i++) {
    int s = 0;
    while (s < 4 && i > stop[s + 1]) s += 1;
    int span = stop[s + 1] - stop[s], t = span > 0 ? ((i - stop[s]) * 256) / span : 0;
    u32 a = color[s], b = color[s + 1];
    int alpha = i < 20 ? 0 : (i > 110 ? 255 : ((i - 20) * 255) / 90);
    firePal[i] = fireColor(
      alpha,
      (((a & 0xff) * (256 - t)) + ((b & 0xff) * t)) >> 8,
      ((((a >> 8) & 0xff) * (256 - t)) + (((b >> 8) & 0xff) * t)) >> 8,
      ((((a >> 16) & 0xff) * (256 - t)) + (((b >> 16) & 0xff) * t)) >> 8
    );
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
  u32 frameBytes = FRAME_STRIDE * SCREEN_HEIGHT * 4;
  sceGuInit();
  sceGuStart(GU_DIRECT, guList);
  sceGuDrawBuffer(GU_PSM_8888, (void*)0, FRAME_STRIDE);
  sceGuDispBuffer(SCREEN_WIDTH, SCREEN_HEIGHT, (void*)frameBytes, FRAME_STRIDE);
  sceGuDepthBuffer((void*)(frameBytes * 2), FRAME_STRIDE);
  sceGuOffset(2048 - SCREEN_WIDTH / 2, 2048 - SCREEN_HEIGHT / 2);
  sceGuViewport(2048, 2048, SCREEN_WIDTH, SCREEN_HEIGHT);
  sceGuDepthRange(65535, 0);
  sceGuScissor(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  sceGuEnable(GU_SCISSOR_TEST);
  sceGuDisable(GU_DEPTH_TEST);
  sceGuDisable(GU_CULL_FACE);
  sceGuEnable(GU_CLIP_PLANES);
  sceGuShadeModel(GU_SMOOTH);
  sceGuEnable(GU_TEXTURE_2D);
  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
  sceGuTexFilter(GU_LINEAR, GU_LINEAR);
  sceGuTexWrap(GU_CLAMP, GU_CLAMP);
  sceGuTexScale(1.0f, 1.0f);
  sceGuTexOffset(0.0f, 0.0f);
  sceGuFinish();
  sceGuSync(0, 0);
  sceDisplayWaitVblankStart();
  sceGuDisplay(GU_TRUE);
}

typedef struct {
  float u, v;
  float x, y, z;
} FireTexVert;

typedef struct {
  u32 color;
  float x, y, z;
} FloorVert;

static void drawGround(void) {
  sceGuDisable(GU_TEXTURE_2D);
  sceGuEnable(GU_BLEND);
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
  sceGumMatrixMode(GU_MODEL);
  sceGumLoadIdentity();
  const int cells = 8;
  const float size = 12.0f / (float)cells;
  FloorVert* v = (FloorVert*)sceGuGetMemory(cells * cells * 6 * sizeof(FloorVert));
  int idx = 0;
  for (int gz = 0; gz < cells; gz++) {
    float z0 = -6.0f + (float)gz * size;
    float z1 = z0 + size;
    for (int gx = 0; gx < cells; gx++) {
      float x0 = -6.0f + (float)gx * size;
      float x1 = x0 + size;
      u32 col = ((gx ^ gz) & 1) ? 0x80101a2c : 0x801c2e4c;
      v[idx++] = { col, x0, 0.0f, z0 };
      v[idx++] = { col, x1, 0.0f, z0 };
      v[idx++] = { col, x0, 0.0f, z1 };
      v[idx++] = { col, x1, 0.0f, z0 };
      v[idx++] = { col, x1, 0.0f, z1 };
      v[idx++] = { col, x0, 0.0f, z1 };
    }
  }
  sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, cells * cells * 6, 0, v);
}

static void drawFireQuad(float x, float z, float w, float h, float yaw, int flipU) {
  ScePspFVector3 pos = { x, 0.0f, z };
  sceGumMatrixMode(GU_MODEL);
  sceGumLoadIdentity();
  sceGumTranslate(&pos);
  sceGumRotateY(yaw);

  float hw = w * 0.5f;
  float u0 = flipU ? 1.0f : 0.0f;
  float u1 = flipU ? 0.0f : 1.0f;
  FireTexVert* v = (FireTexVert*)sceGuGetMemory(6 * sizeof(FireTexVert));
  v[0] = { u0, 1.0f, -hw, 0.0f, 0.0f };
  v[1] = { u1, 1.0f,  hw, 0.0f, 0.0f };
  v[2] = { u0, 0.0f, -hw,   h,  0.0f };
  v[3] = { u1, 1.0f,  hw, 0.0f, 0.0f };
  v[4] = { u1, 0.0f,  hw,   h,  0.0f };
  v[5] = { u0, 0.0f, -hw,   h,  0.0f };
  sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 6, 0, v);
}

static u32 nextClearColor(void) {
  static u32 rng = 0x13579bdfu;
  static int start = 256;
  static int target = 256;
  static int cur = 256;
  static int step = 1;
  static int span = 1;
  if (step >= span) {
    rng = rng * 1664525u + 1013904223u;
    start = cur;
    target = (int)((rng >> 16) & 0xffu);
    if (target >= 128) target += 1;
    rng = rng * 1664525u + 1013904223u;
    span = 8 + (int)((rng >> 24) & 11u);
    step = 0;
  }
  step += 1;
  cur = start + ((target - start) * step) / span;
  int r = 6 + ((60 - 6) * cur) / 256;
  int g = 3 + ((26 - 3) * cur) / 256;
  int b = 1 + ((8 - 1) * cur) / 256;
  return 0xff000000u | ((u32)b << 16) | ((u32)g << 8) | (u32)r;
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
