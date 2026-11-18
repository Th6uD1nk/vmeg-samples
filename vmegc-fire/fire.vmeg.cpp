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
static u32 __attribute__((aligned(16))) guList[256];
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

static void firePaint(const volatile u32* field, const u32* palette) {
  sceGuStart(GU_DIRECT, guList);
  sceGuClearColor(0xff000000);
  sceGuClear(GU_COLOR_BUFFER_BIT);
  sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
  sceGuClutLoad(32, palette);
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

int main() {
  scePowerSetClockFrequency(333, 333, 166);
  SceCtrlData ctl;
  int running = 1;
  while (running) {
    sceDisplayWaitVblankStart();
    sceCtrlPeekBufferPositive(&ctl, 1);
    if (ctl.Buttons & PSP_CTRL_HOME) {
      running = 0;
    }
  }
  sceKernelExitGame();
  return 0;
}
