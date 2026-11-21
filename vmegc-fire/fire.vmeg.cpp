#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <psppower.h>
#include <me-core-mapper/me-core.h>
#include <me-core-mapper/vme-fu-opcodes.h>

PSP_MODULE_INFO("vme-fire", 0, 1, 1);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_VFPU | PSP_THREAD_ATTR_USER);

PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(1024);
ME_LIB_SETUP_SIMPLE_SUSPEND_HANDLER();

#define FIRE_ROW_CELLS 128
#define FIRE_HEIGHT 64
#define FIRE_ROWS 63
#define FIRE_FIELD (FIRE_ROW_CELLS * (FIRE_HEIGHT + 2))
#define FIRE_BAND_ROWS 8
#define FIRE_BAND_CELLS (FIRE_ROW_CELLS * FIRE_BAND_ROWS)
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 272
#define FRAME_STRIDE 512

meLibSetSharedUncached32(16);
#define fireRequest (meLibSharedMemory[1])
#define fireDone (meLibSharedMemory[2])

volatile Buffer heatA[FIRE_FIELD];
volatile Buffer heatB[FIRE_FIELD];
volatile Buffer("Q16.8") waveX[FIRE_FIELD];
volatile Buffer("Q16.8") waveY[FIRE_FIELD];
volatile Buffer waveSway[FIRE_FIELD];
volatile Buffer seedState[FIRE_ROW_CELLS];
volatile Buffer fireEnvelopes[120][FIRE_ROW_CELLS];
static u32 firePal[256] __attribute__((aligned(64)));

static u32 __attribute__((aligned(16))) guList[8192];

static u32 fireColor(int a, int r, int g, int b) {

  return ((u32)a << 24) | ((u32)b << 16) | ((u32)g << 8) | (u32)r;
}

static int clamp255(int v) {

  return v < 0 ? 0 : (v > 255 ? 255 : v);
}

static int fireRamp(int x, int lo, int hi) {

  return clamp255(((x - lo) * 255) / (hi - lo));
}

static void firePaletteInit(void) {

  for (int i = 0; i < 256; i++) {

    firePal[i] = fireColor(
      fireRamp(i, 20, 110),
      fireRamp(i, 0, 112),
      fireRamp(i, 128, 240),
      fireRamp(i, 176, 255)
    );
  }
}

static void fireEnvelopeInit(void) {

  for (int p = 0; p < 120; p++) {

    int breath = 210 + ((p < 60 ? p : 120 - p) * 45) / 59;
    for (int x = 0; x < FIRE_ROW_CELLS / 2; x++) {

      int ad = (FIRE_ROW_CELLS - 1) - x * 2;
      int window = clamp255(255 - ad * 2 - (ad * ad) / 140);
      int tri = (ad + p / 2) % 22;
      int jag = fireRamp(tri < 11 ? tri : 22 - tri, 0, 11);
      int teeth = 255 - (jag * fireRamp(ad, 0, 110)) / 255;
      u32 v = (u32)((((window * breath) >> 8) * teeth) >> 8);
      fireEnvelopes[p][x] = v;
      fireEnvelopes[p][FIRE_ROW_CELLS - 1 - x] = v;
    }
  }
}

static void fireFieldClear(void) {

  for (int i = 0; i < FIRE_FIELD; i++) {

    heatA[i] = 0;
    heatB[i] = 0;
  }
  int wx = 185;
  int wy = 0;
  for (int y = 0; y < FIRE_HEIGHT; y++) {

    int amp = FIRE_HEIGHT - y;
    int sx = (wx * amp) >> 6;
    int sy = (wy * amp) >> 6;
    for (int x = 0; x < FIRE_ROW_CELLS; x++) {

      waveX[y * FIRE_ROW_CELLS + x] = (u32)sx;
      waveY[y * FIRE_ROW_CELLS + x] = (u32)sy;
    }
    wx += wy >> 4;
    wy -= wx >> 4;
  }
  for (int i = 0; i < FIRE_ROW_CELLS; i++) {

    seedState[i] = (u32)(i * 37 + 1) * 2654435761u;
  }
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

typedef struct {

  float start, target;
  int step, span;
} FireScale;

static float fireScaleStep(FireScale* s, u32* rng, float lo, float hi, int minSpan, int spanMask) {

  if (s->step >= s->span) {

    u32 v = *rng = *rng * 1664525u + 1013904223u;
    s->start = s->span ? s->target : 1.0f;
    s->target = lo + (hi - lo) * ((float)((v >> 16) & 0xffu) / 255.0f);
    s->span = minSpan + (int)((v >> 24) & (u32)spanMask);
    s->step = 0;
  }
  float u = (float)(++s->step) / (float)s->span;
  return s->start + (s->target - s->start) * u * u * (3.0f - 2.0f * u);
}

static u32 nextClearColor(void) {

  static FireScale glow;
  static u32 rng = 0x13579bdfu;
  float t = fireScaleStep(&glow, &rng, 0.0f, 1.0f, 8, 11);
  return fireColor(255, 6 + (int)(54.0f * t), 3 + (int)(23.0f * t), 1 + (int)(7.0f * t));
}

static void firePaint3D(const volatile u32* field, float camAngle) {

  static const struct {

    float x, z, w, h;
    int flip;
  } fires[12] = {
    { -1.6f, -1.4f, 2.36f, 2.30f, 0 },
    {  1.7f, -1.5f, 1.32f, 2.86f, 1 },
    { -1.5f,  1.7f, 2.08f, 1.85f, 0 },
    {  1.6f,  1.6f, 1.20f, 2.54f, 1 },
    { -3.9f,  0.2f, 2.46f, 1.97f, 0 },
    {  3.9f, -0.2f, 1.62f, 2.76f, 0 },
    {  0.2f, -3.9f, 2.20f, 1.42f, 1 },
    { -0.2f,  3.9f, 1.48f, 2.98f, 1 },
    { -3.4f, -3.5f, 1.86f, 1.63f, 0 },
    {  3.5f, -3.3f, 1.08f, 2.40f, 1 },
    { -3.3f,  3.5f, 2.28f, 1.51f, 1 },
    {  3.4f,  3.4f, 1.70f, 2.62f, 0 },
  };
  static FireScale scaleX[12];
  static FireScale scaleY[12];
  static u32 scaleRng = 0x2468ace1u;

  sceGuStart(GU_DIRECT, guList);
  sceGuClearColor(nextClearColor());
  sceGuClear(GU_COLOR_BUFFER_BIT);

  sceGumMatrixMode(GU_PROJECTION);
  sceGumLoadIdentity();
  sceGumPerspective(52.0f, 480.0f / 272.0f, 0.3f, 40.0f);

  sceGumMatrixMode(GU_VIEW);
  sceGumLoadIdentity();
  ScePspFVector3 viewPos = { 0.0f, -2.0f, -9.5f };
  sceGumTranslate(&viewPos);
  sceGumRotateX(0.22f);
  sceGumRotateY(camAngle);

  drawGround();

  sceGuEnable(GU_TEXTURE_2D);
  sceGuEnable(GU_BLEND);
  sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xffffffff, 0xffffffff);
  sceGuTexFilter(GU_LINEAR, GU_LINEAR);
  sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
  sceGuClutLoad(32, firePal);
  sceGuTexMode(GU_PSM_T32, 0, 0, 0);
  sceGuTexImage(0, 128, FIRE_HEIGHT, 128, (const void*)field);

  for (int i = 0; i < 12; i++) {

    float sx = fireScaleStep(&scaleX[i], &scaleRng, 0.88f, 1.14f, 36, 31);
    float sy = fireScaleStep(&scaleY[i], &scaleRng, 0.84f, 1.18f, 28, 31);
    drawFireQuad(fires[i].x, fires[i].z, fires[i].w * sx, fires[i].h * sy, -camAngle, fires[i].flip);
  }

  sceGuFinish();
  sceGuSync(0, 0);
}

.kernel fireWave extended {

  Transfer xIn = { memory: waveXPtr, count: FIRE_BAND_CELLS - 1 };
  Transfer yIn = { memory: waveYPtr, count: FIRE_BAND_CELLS - 1 };
  Transfer xOut = { memory: waveXPtr, count: FIRE_BAND_CELLS - 1 };
  Transfer yOut = { memory: waveYPtr, count: FIRE_BAND_CELLS - 1 };
  Transfer sOut = { memory: waveSwayPtr, count: FIRE_BAND_CELLS - 1 };

  .load {

    .transfer = xIn;
    .scope = local;
    .offsets {

      .top0 = 0;
    }
  }

  .load {

    .transfer = yIn;
    .scope = local;
    .offsets {

      .top1 = 0;
      .top2 = 0;
    }
  }

  Agu lanes = { count: FIRE_BAND_CELLS - 1 };
  Agu writeY = { count: FIRE_BAND_CELLS - 1, cyc: auto };
  Agu writeS = { count: FIRE_BAND_CELLS - 1, cyc: auto };
  Staging x0, y0, nx, y1;

  .pe0 {

    .top = lanes;
    x0 = top0;
  }

  .pe1 {

    .top = lanes;
    y0 = top1;
    nx = x0 + (y0 >> 4);
  }

  .pe2 {

    .top = lanes;
    y1 = top2;
  }

  .pe4 {

    .write = writeY;
    .spad = y1 - (nx >> 4);
  }

  .pe5 {

    .write = writeS;
    .spad = nx >> 6;
  }

  .store {

    .transfer = xOut;
    .scope = local;
    .offsets {

      .base1 = 0;
    }
  }

  .store {

    .transfer = yOut;
    .scope = local;
    .offsets {

      .base4 = 0;
    }
  }

  .store {

    .transfer = sOut;
    .scope = local;
    .offsets {

      .base5 = 0;
    }
  }
}

.kernel fireStep extended {

  Transfer leftIn = { memory: leftPtr, count: FIRE_BAND_CELLS - 1 };
  Transfer centerIn = { memory: centerPtr, count: FIRE_BAND_CELLS - 1 };
  Transfer rightIn = { memory: rightPtr, count: FIRE_BAND_CELLS - 1 };
  Transfer farIn = { memory: farPtr, count: FIRE_BAND_CELLS - 1 };
  Transfer waveIn = { memory: waveSwayPtr, count: FIRE_BAND_CELLS - 1 };
  Transfer outBand = { memory: outPtr, count: FIRE_BAND_CELLS - 1 };

  .load {

    .transfer = leftIn;
    .scope = local;
    .offsets {

      .top0 = 0;
    }
  }

  .load {

    .transfer = centerIn;
    .scope = local;
    .offsets {

      .top1 = 0;
    }
  }

  .load {

    .transfer = rightIn;
    .scope = local;
    .offsets {

      .top2 = 0;
    }
  }

  .load {

    .transfer = farIn;
    .scope = local;
    .offsets {

      .top3 = 0;
    }
  }

  .load {

    .transfer = waveIn;
    .scope = local;
    .offsets {

      .top4 = 0;
    }
  }

  Agu lanes = { count: FIRE_BAND_CELLS - 1 };
  Agu writeOut = { count: FIRE_BAND_CELLS - 1, cyc: auto };
  Staging l, c, r, f, low, high, diff, total, tilt, adv, cooled, out;

  .pe0 {

    .top = lanes;
    l = top0;
  }

  .pe1 {

    .top = lanes;
    c = top1;
    low = l + c;
  }

  .pe2 {

    .top = lanes;
    r = top2;
    diff = l - r;
  }

  .pe3 {

    .top = lanes;
    f = top3;
    high = r + f;
  }

  .pe4 {

    .top = { count: FIRE_BAND_CELLS - 1 };
    tilt = mulclp(top4, diff);
    total = low + high;
  }

  .pe5 {

    adv = total + (tilt >> 1);
  }

  .pe6 {

    cooled = (adv * 62) >> 8;
  }

  .pe7 {

    .write = writeOut;
    out = (cooled & 128) ? cooled + 2 : cooled - 2;
    .spad = clamp(out, 0, 255);
  }

  .store {

    .transfer = outBand;
    .scope = local;
    .offsets {

      .base7 = 0;
    }
  }
}

.kernel fireSeed extended {

  Transfer stateIn = { memory: statePtr, count: FIRE_ROW_CELLS - 1 };
  Transfer envelopeIn = { memory: envelopePtr, count: FIRE_ROW_CELLS - 1 };
  Transfer stateOut = { memory: statePtr, count: FIRE_ROW_CELLS - 1 };
  Transfer rowOut = { memory: rowPtr, count: FIRE_ROW_CELLS - 1 };

  .load {

    .transfer = stateIn;
    .scope = local;
    .offsets {

      .top0 = 0;
    }
  }

  .load {

    .transfer = envelopeIn;
    .scope = local;
    .offsets {

      .top4 = 0;
      .top7 = 2;
    }
  }

  Agu lanes = { count: FIRE_ROW_CELLS - 1 };
  Agu writeState = { count: FIRE_ROW_CELLS - 1, cyc: auto };
  Agu writeHeat = { count: FIRE_ROW_CELLS - 1, cyc: auto };
  Staging x1, a, x2, b, x3, c, x4, amp, e2, env, prod;

  .pe0 {

    .top = lanes;
    x1 = top0 << 7;
    a = top0 ^ x1;
  }

  .pe1 {

    x2 = a >> 9;
    b = a ^ x2;
  }

  .pe2 {

    x3 = b << 8;
    c = b ^ x3;
  }

  .pe3 {

    x4 = c & 63;
    amp = x4 + 192;
  }

  .pe7 {

    .top = { count: FIRE_ROW_CELLS - 1, cyc: auto };
    e2 = top7 >> 1;
  }

  .pe4 {

    .top = { count: FIRE_ROW_CELLS - 1, cyc: auto };
    env = top4 + e2;
  }

  .pe5 {

    .write = writeState;
    .spad = c;
  }

  .pe6 {

    .write = writeHeat;
    prod = (amp * env) >> 8;
    .spad = clamp(prod, 0, 255);
  }

  .store {

    .transfer = stateOut;
    .scope = local;
    .offsets {

      .base5 = 0;
    }
  }

  .store {

    .transfer = rowOut;
    .scope = local;
    .offsets {

      .base6 = 0;
    }
  }
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
      volatile Buffer* waveXPtr = waveX + row * FIRE_ROW_CELLS;
      volatile Buffer* waveYPtr = waveY + row * FIRE_ROW_CELLS;
      volatile Buffer* waveSwayPtr = waveSway + row * FIRE_ROW_CELLS;
      @kernel fireWave config | exec {
      }
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
  float camAngle = 0.0f;
  fireRequest = request;

  do {

    sceCtrlPeekBufferPositive(&ctl, 1);
    const volatile u32* ready = (fireDone & 1) ? heatB : heatA;
    camAngle += 0.0056f;
    firePaint3D(ready, camAngle);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
    if (fireDone == request) {

      request += 1;
      fireRequest = request;
    }
  } while (!(ctl.Buttons & PSP_CTRL_HOME));
  sceGuTerm();
  sceKernelExitGame();
  return 0;
}
