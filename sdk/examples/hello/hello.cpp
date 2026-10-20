// Hello: the smallest useful native mod, in C++ with sms_mod.hpp.
// It names the stage as it loads, counts the coins picked up in it, draws
// both in a HUD slot at the bottom left, and hides them with a key (H, or
// key.toggle in its settings) or a setting in the Mods menu (F1).
#include <sms_mod.hpp>

#include <stdio.h>

struct Hello : sms::Mod {
	using sms::Mod::Mod;

	int coins   = 0;
	bool shown  = true;
	int toggle  = -1;
	char where[96] = "";

	int load() override
	{
		toggle = bindKey("toggle", "H");
		optionBool("show", "Show the greeting", true);

		on<SmsEvStage>(SMS_EV_STAGE_LOADED, [this](SmsEvStage& s) {
			coins = 0;
			const char* name = game().area_name(s.area);
			snprintf(where, sizeof where, "%s, episode %d", *name ? name : "somewhere", s.episode + 1);
			info("hello from %s", where);
		});

		on<SmsEvItem>(SMS_EV_ITEM_COLLECTED, [this](SmsEvItem& item) {
			if (item.kind == SMS_ITEM_COIN)
				coins++;
		});

		// a key of ours: consumed, so the game's controls never see it
		on<SmsEvKey>(SMS_EV_KEY, [this](SmsEvKey& k) {
			if (k.scancode != toggle)
				return SMS_CONTINUE;
			if (k.down && !k.repeat)
				shown = !shown;
			return SMS_CANCEL;
		});

		on<SmsEvDraw>(SMS_EV_DRAW, [this](SmsEvDraw& d) {
			if (d.mode != SMS_MODE_STAGE || state().paused || !shown || !setting("show", 1))
				return;
			char line[128];
			snprintf(line, sizeof line, "Hello from %s!  Coins: %d", where, coins);
			const float w = draw().text_width(14, line) + 20;
			SmsRect r;
			if (!draw().hud_slot(handle(), SMS_ANCHOR_BOTTOM_LEFT, w, 24, &r))
				return;
			draw().rect(r.x, r.y, r.w, r.h, SMS_RGBA(0, 0, 0, 160));
			draw().text(r.x + 10, r.y + 6, 14, SMS_RGB(255, 230, 120), SMS_TEXT_SHADOW, line);
		});
		return 0;
	}
};

SMS_MOD_CLASS(Hello)
