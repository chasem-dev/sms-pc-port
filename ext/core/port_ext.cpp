// The extension registry (src/port_include/port_ext.h): the game's hooks call
// each registered extension in the order they registered. Built into the
// game's library with the game's own flags, so it keeps to C++03 and fixed
// arrays (no allocation: the game's operator new is its JKR heaps').
#include "port_ext.h"

#include <string.h>

namespace {

enum { kMaxExtensions = 16 };
const PortExtension* s_ext[kMaxExtensions];
int s_count;

} // namespace

extern "C" void port_ext_register(const PortExtension* ext)
{
	if (!ext || s_count >= kMaxExtensions)
		return;
	for (int i = 0; i < s_count; i++)
		if (s_ext[i] == ext)
			return;
	s_ext[s_count++] = ext;
}

extern "C" const PortExtension* port_ext_find(const char* name)
{
	for (int i = 0; i < s_count; i++)
		if (s_ext[i]->name && name && !strcmp(s_ext[i]->name, name))
			return s_ext[i];
	return 0;
}

#define EACH(hook) \
	for (int i = 0; i < s_count; i++) \
		if (s_ext[i]->hook)

extern "C" void port_ext_frame(void)
{
	EACH(frame) s_ext[i]->frame();
}

extern "C" void port_ext_stage_setup(void)
{
	EACH(stageSetup) s_ext[i]->stageSetup();
}

extern "C" int port_ext_allow_next_stage(unsigned code, void* actor)
{
	EACH(allowNextStage) if (!s_ext[i]->allowNextStage(code, actor)) return 0;
	return 1;
}

extern "C" void port_ext_move_stage(void)
{
	EACH(moveStage) s_ext[i]->moveStage();
}

extern "C" void port_ext_miss_next_area(void)
{
	EACH(missNextArea) s_ext[i]->missNextArea();
}

extern "C" int port_ext_keep_coins_on_miss(void)
{
	EACH(keepCoinsOnMiss) if (s_ext[i]->keepCoinsOnMiss()) return 1;
	return 0;
}

extern "C" void port_ext_after_file_select(void)
{
	EACH(afterFileSelect) s_ext[i]->afterFileSelect();
}

extern "C" void port_ext_item_taken(void* item, void* taker)
{
	EACH(itemTaken) s_ext[i]->itemTaken(item, taker);
}

extern "C" int port_ext_allow_save_states(const char** reason)
{
	EACH(allowSaveStates) if (!s_ext[i]->allowSaveStates()) {
		if (reason)
			*reason = s_ext[i]->saveStatesOffReason ? s_ext[i]->saveStatesOffReason() : "Save states are off";
		return 0;
	}
	return 1;
}

extern "C" int port_ext_hide_player_model(void* mario)
{
	EACH(hidePlayerModel) if (s_ext[i]->hidePlayerModel(mario)) return 1;
	return 0;
}
