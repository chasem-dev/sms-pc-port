// Storage for SDK globals the game references directly. Weak where another
// platform module (GX/GD, audio) is expected to own the real definition.
#include "port_compat.h"
#include <dolphin/gx.h>
#include <dolphin/gd.h>
#include <dolphin/dsp.h>

// GD display-list builder state (J3D/GD writers use it inline).
__attribute__((weak)) GDLObj* __GDCurrentDL;
extern "C" __attribute__((weak)) void GDOverflowed(void)
{
	fprintf(stderr, "[gd] display list overflow\n");
}

// Standard NTSC 640x480 interlaced render mode (values from the public SDK
// documentation of GXNtsc480Int).
__attribute__((weak)) GXRenderModeObj GXNtsc480Int = {
	VI_TVMODE_NTSC_INT, 640, 480, 480, 40, 0, 640, 480, VI_XFBMODE_DF, GX_FALSE, GX_FALSE,
	{ { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 } },
	{ 0, 0, 21, 22, 21, 0, 0 },
};

// DSP task list (the task manager in JSystem/osdsp_task.c drives it). There is
// no DSP on the host; the audio layer will replace this.
DSPTaskInfo* __DSP_curr_task;
DSPTaskInfo* __DSP_first_task;
DSPTaskInfo* __DSP_last_task;
extern "C" void __DSP_boot_task(DSPTaskInfo* task) { fprintf(stderr, "[dsp] boot task %p (no DSP)\n", task); }
extern "C" void __DSP_insert_task(DSPTaskInfo* task) {}
extern "C" void __DSP_exec_task(DSPTaskInfo* curr, DSPTaskInfo* next) {}
extern "C" void __DSP_remove_task(DSPTaskInfo* task) {}
