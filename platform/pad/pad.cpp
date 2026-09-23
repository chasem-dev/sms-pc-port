// PAD: controller 1 is connected and idle; ports 2-4 are empty. Keyboard/SDL
// input plugs in here later.
#include "port_compat.h"
#include "port_platform.h"
#include <dolphin/pad.h>

extern "C" BOOL PADInit() { return TRUE; }
extern "C" int PADReset(unsigned long) { return TRUE; }
extern "C" BOOL PADRecalibrate(u32) { return TRUE; }
extern "C" BOOL PADSync(void) { return TRUE; }
extern "C" void PADSetSpec(u32) {}
extern "C" void PADSetAnalogMode(u32) {}
extern "C" void PADSetSamplingRate(unsigned long) {}
extern "C" void PADControlMotor(s32, u32) {}
extern "C" void PADControlAllMotors(const u32*) {}
extern "C" u32 PADRead(PADStatus* status)
{
	for (int i = 0; i < 4; i++) {
		memset(&status[i], 0, sizeof status[i]);
		status[i].err = i == 0 ? PAD_ERR_NONE : PAD_ERR_NO_CONTROLLER;
	}
	return 0;
}
extern "C" void PADClamp(PADStatus*) {}
