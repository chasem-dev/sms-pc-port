/* Stage Timer: an example native mod for the Super Mario Sunshine PC port.
 *
 * Counts the stage's in-game time in simulation ticks (it stops while the
 * game is paused), splits at each Shine, keeps the best time of each stage
 * and episode in its settings, and counts the hits Mario takes.
 *
 * It is written to sit beside other mods:
 *   - its panel takes a HUD slot at the same anchor as the trainer HUD's, so
 *     the two stack instead of overlapping (and it loads after it, mod.ini);
 *   - it watches damage at MONITOR priority and leaves out hits another mod
 *     cancelled (the trainer HUD's invincibility);
 *   - it tells other mods of each split with a custom event,
 *     "stage-timer.split", which the trainer HUD shows when it is there.
 *   - F4 (key.reset in its settings) starts the timer again.
 */
#include <sms_mod.h>

#include <stdio.h>
#include <string.h>

static const SmsApi* api;
static SmsMod* self;

static int s_area = -1, s_episode = -1;
static uint32_t s_ticks;      /* this stage's in-game time */
static int s_tps = 30;
static int s_running;
static int s_hits;
static float s_lastSplit = -1;
static float s_best = -1;     /* this stage's best, seconds */
static int s_resetKey = -1;
static uint32_t s_splitEvent;

typedef struct Split {
	float seconds;
	int shine;
} Split;

static float seconds(void) { return (float)s_ticks / (float)s_tps; }

static void bestKey(char* out, size_t n) { snprintf(out, n, "best.%d.%d", s_area, s_episode); }

static void format(char* out, size_t n, float t)
{
	if (t < 0) {
		snprintf(out, n, "--:--.--");
		return;
	}
	const int m = (int)(t / 60);
	snprintf(out, n, "%d:%05.2f", m, t - m * 60.0f);
}

static int onStage(SmsMod* mod, SmsEvent* ev, void* user)
{
	(void)user;
	const SmsEvStage* st = (const SmsEvStage*)ev->data;
	s_area      = st->area;
	s_episode   = st->episode;
	s_ticks     = 0;
	s_hits      = 0;
	s_running   = 1;
	s_lastSplit = -1;
	char key[32];
	bestKey(key, sizeof key);
	s_best = api->config->get_float(mod, key, -1);
	return SMS_CONTINUE;
}

static int onTick(SmsMod* mod, SmsEvent* ev, void* user)
{
	(void)mod;
	(void)user;
	const SmsEvTick* t = (const SmsEvTick*)ev->data;
	s_tps = t->ticks_per_second;
	if (s_running)
		s_ticks++;
	return SMS_CONTINUE;
}

static int onShine(SmsMod* mod, SmsEvent* ev, void* user)
{
	(void)user;
	const SmsEvShine* sh = (const SmsEvShine*)ev->data;
	const float t        = seconds();
	s_lastSplit          = t;
	char text[16];
	format(text, sizeof text, t);
	api->core->log(mod, SMS_LOG_INFO, "split: Shine %d at %s (area %d, episode %d)", sh->shine, text, s_area,
	               s_episode + 1);
	if (s_best < 0 || t < s_best) {
		char key[32];
		bestKey(key, sizeof key);
		s_best = t;
		api->config->set_float(mod, key, t);
		api->config->save(mod);
	}
	Split split = {t, sh->shine};
	api->core->emit(mod, s_splitEvent, &split, sizeof split);
	return SMS_CONTINUE;
}

static int onDamage(SmsMod* mod, SmsEvent* ev, void* user)
{
	(void)mod;
	(void)user;
	if (!ev->cancelled) /* another mod may have stopped it */
		s_hits++;
	return SMS_CONTINUE;
}

static int onKey(SmsMod* mod, SmsEvent* ev, void* user)
{
	(void)user;
	const SmsEvKey* k = (const SmsEvKey*)ev->data;
	if (k->scancode != s_resetKey)
		return SMS_CONTINUE;
	if (k->down && !k->repeat) {
		s_ticks     = 0;
		s_hits      = 0;
		s_lastSplit = -1;
		api->core->log(mod, SMS_LOG_INFO, "timer reset");
	}
	return SMS_CANCEL;
}

static int onDraw(SmsMod* mod, SmsEvent* ev, void* user)
{
	(void)user;
	const SmsEvDraw* e  = (const SmsEvDraw*)ev->data;
	const SmsDrawApi* d = api->draw;
	SmsGameState st;
	st.size = sizeof st;
	api->game->state(&st);
	if (e->mode != SMS_MODE_STAGE || st.paused || s_area < 0 || !api->config->get_int(mod, "show", 1))
		return SMS_CONTINUE;
	SmsRect r;
	if (!d->hud_slot(mod, SMS_ANCHOR_TOP_LEFT, 176, 42, &r))
		return SMS_CONTINUE;
	d->rect(r.x, r.y, r.w, r.h, SMS_RGBA(30, 10, 40, 190));
	d->rect(r.x, r.y, 3, r.h, SMS_RGB(120, 255, 160));
	char now[16], best[16], split[16], line[64];
	format(now, sizeof now, seconds());
	format(best, sizeof best, s_best);
	format(split, sizeof split, s_lastSplit);
	d->text(r.x + 8, r.y + 4, 20, SMS_RGB(255, 255, 255), SMS_TEXT_SHADOW, now);
	snprintf(line, sizeof line, "HITS %d", s_hits);
	d->text(r.x + r.w - 6, r.y + 6, 10, SMS_RGB(255, 160, 160), SMS_TEXT_RIGHT, line);
	if (api->config->get_int(mod, "show_best", 1)) {
		snprintf(line, sizeof line, "BEST %s   SPLIT %s", best, split);
		d->text(r.x + 8, r.y + 28, 10, SMS_RGB(170, 230, 190), 0, line);
	}
	return SMS_CONTINUE;
}

static void resetBest(SmsMod* mod, const char* key, void* user)
{
	(void)key;
	(void)user;
	/* every best time: write them as unset */
	for (int area = 0; area < 64; area++)
		for (int ep = 0; ep < 16; ep++) {
			char k[32];
			snprintf(k, sizeof k, "best.%d.%d", area, ep);
			if (api->config->get_float(mod, k, -1) >= 0)
				api->config->set_float(mod, k, -1);
		}
	s_best = -1;
	api->config->save(mod);
	api->core->log(mod, SMS_LOG_INFO, "best times cleared");
}

SMS_MOD_DEFINE()

SMS_MOD_EXTERN_C SMS_MOD_EXPORT int sms_mod_load(SmsMod* mod, const SmsApi* a)
{
	api  = a;
	self = mod;
	const SmsCoreApi* c = api->core;
	c->subscribe(mod, SMS_EV_STAGE_LOADED, onStage, NULL, SMS_PRIORITY_NORMAL);
	c->subscribe(mod, SMS_EV_TICK, onTick, NULL, SMS_PRIORITY_NORMAL);
	c->subscribe(mod, SMS_EV_SHINE_COLLECTED, onShine, NULL, SMS_PRIORITY_NORMAL);
	c->subscribe(mod, SMS_EV_PLAYER_DAMAGE, onDamage, NULL, SMS_PRIORITY_MONITOR);
	c->subscribe(mod, SMS_EV_KEY, onKey, NULL, SMS_PRIORITY_NORMAL);
	c->subscribe(mod, SMS_EV_DRAW, onDraw, NULL, SMS_PRIORITY_NORMAL);
	s_splitEvent = c->event_id("stage-timer.split");
	s_resetKey   = api->input->bind_key(mod, "reset", "F4");

	SmsOption o;
	memset(&o, 0, sizeof o);
	o.size  = sizeof o;
	o.key   = "show";
	o.label = "Show the timer";
	o.type  = SMS_OPTION_BOOL;
	o.def   = 1;
	api->config->add_option(mod, &o);
	o.key   = "show_best";
	o.label = "Show best and split";
	api->config->add_option(mod, &o);
	memset(&o, 0, sizeof o);
	o.size      = sizeof o;
	o.key       = "reset_best";
	o.label     = "Clear best times";
	o.type      = SMS_OPTION_ACTION;
	o.on_change = resetBest;
	api->config->add_option(mod, &o);

	/* a stage already being played (loaded from the Mods menu mid-stage) */
	SmsGameState st;
	st.size = sizeof st;
	api->game->state(&st);
	if (st.stage_ready) {
		s_area    = st.area;
		s_episode = st.episode;
		s_running = 1;
	}
	return 0;
}
