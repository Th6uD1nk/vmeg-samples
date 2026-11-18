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
