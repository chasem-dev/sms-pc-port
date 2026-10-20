/* Trainer HUD: an example native mod for the Super Mario Sunshine PC port.
 *
 * It shows most of what the mod API offers:
 *   - a panel of Mario's speed, position, health and water, with a speed
 *     graph, in a HUD slot (draw->hud_slot) other mods' panels stack around;
 *   - a trail behind Mario and a ring where he will land, drawn in the world;
 *   - "+1" pop-ups where coins were taken (SMS_EV_ITEM_COLLECTED);
 *   - the stage's name as it loads, and a banner for each new Shine;
 *   - a field of view setting (SMS_EV_CAMERA), invincibility (cancelling
 *     SMS_EV_PLAYER_DAMAGE) and a moon jump (player->set_velocity);
 *   - settings in the Mods menu (F1), a key of its own (F3 shows and hides the
 *     panel), an image made from pixels and one read from a PNG;
 *   - the custom event another mod sends ("stage-timer.split"), shown as a
 *     pop-up when that mod is there, and nothing when it is not.
 */
#include <sms_mod.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static const SmsApi* api;
static SmsMod* self;

/* settings */
static int s_panel = 1;   /* F3 */
static int s_toggleKey = -1, s_moonKey = -1;
#define OPT(key, def) api->config->get_int(self, key, def)

/* the trail: a point every few ticks */
#define TRAIL_MAX 240
static SmsVec3 s_trail[TRAIL_MAX];
static int s_trailHead, s_trailCount;
static uint32_t s_lastTick;

/* speed: measured from where Mario is each tick, units a second */
#define GRAPH_N 120
static float s_speed[GRAPH_N];
static int s_speedHead;
static SmsVec3 s_lastPos;
static int s_havePos;
static float s_smoothSpeed;

/* pop-ups in the world */
typedef struct Popup {
	SmsVec3 at;
	double born;
	char text[32];
	SmsColor color;
} Popup;
#define POPUPS 24
static Popup s_popups[POPUPS];
static int s_popupNext;

/* banners at the top of the screen */
static char s_banner[96];
static double s_bannerUntil;

static SmsImage s_dropIcon, s_logo;
static uint32_t s_splitEvent;

static void popup(const SmsVec3* at, const char* text, SmsColor color)
{
	Popup* p = &s_popups[s_popupNext++ % POPUPS];
	p->at    = *at;
	p->born  = api->core->time();
	p->color = color;
	snprintf(p->text, sizeof p->text, "%s", text);
}

static void banner(const char* text, double seconds)
{
	snprintf(s_banner, sizeof s_banner, "%s", text);
	s_bannerUntil = api->core->time() + seconds;
}

/* ------------------------------------------------------------------ events */

static int onStage(SmsMod* mod, SmsEvent* ev, void* user)
{
	(void)mod;
	(void)user;
	const SmsEvStage* st = (const SmsEvStage*)ev->data;
	s_trailCount = s_trailHead = 0;
	s_havePos    = 0;
	const char* name = api->game->area_name(st->area);
	char text[96];
	if (name && *name)
		snprintf(text, sizeof text, "%s  (area %d, episode %d)", name, st->area, st->episode + 1);
	else
		snprintf(text, sizeof text, "Area %d, episode %d", st->area, st->episode + 1);
	banner(text, 4.0);
	api->core->log(self, SMS_LOG_INFO, "stage: %s", text);
	return SMS_CONTINUE;
}

static int onTick(SmsMod* mod, SmsEvent* ev, void* user)
{
	(void)mod;
	(void)user;
	const SmsEvTick* t = (const SmsEvTick*)ev->data;
	SmsPlayer p;
	p.size = sizeof p;
	if (!api->player->get(&p))
		return SMS_CONTINUE;
	/* horizontal speed from the distance moved */
	if (s_havePos) {
		const float dx = p.position.x - s_lastPos.x, dz = p.position.z - s_lastPos.z;
		float v = sqrtf(dx * dx + dz * dz) / t->dt;
		if (v > 20000.0f) /* a warp, not a run */
			v = 0;
		s_smoothSpeed += (v - s_smoothSpeed) * 0.2f;
	}
	s_lastPos = p.position;
	s_havePos = 1;
	if (t->tick % (uint32_t)(t->ticks_per_second / 30 > 0 ? t->ticks_per_second / 30 : 1) == 0) {
		s_speed[s_speedHead] = s_smoothSpeed;
		s_speedHead          = (s_speedHead + 1) % GRAPH_N;
	}
	/* the trail, a point every 1/10 s */
	const uint32_t every = (uint32_t)(t->ticks_per_second / 10 > 0 ? t->ticks_per_second / 10 : 1);
	if (t->tick - s_lastTick >= every || t->tick < s_lastTick) {
		s_lastTick                = t->tick;
		s_trail[s_trailHead]      = p.position;
		s_trailHead               = (s_trailHead + 1) % TRAIL_MAX;
		if (s_trailCount < TRAIL_MAX)
			s_trailCount++;
	}
	/* moon jump: hold the key in the air to rise */
	if (OPT("moon_jump", 0) && s_moonKey >= 0 && api->input->key_down(s_moonKey) && !p.on_ground) {
		SmsVec3 v = p.velocity;
		const float lift = 40.0f * 30.0f / (float)t->ticks_per_second;
		if (v.y < lift)
			v.y = lift;
		api->player->set_velocity(&v);
	}
	return SMS_CONTINUE;
}

static int onItem(SmsMod* mod, SmsEvent* ev, void* user)
{
	(void)mod;
	(void)user;
	const SmsEvItem* it = (const SmsEvItem*)ev->data;
	if (!OPT("coin_popups", 1))
		return SMS_CONTINUE;
	SmsVec3 at = it->position;
	at.y += 80;
	switch (it->kind) {
	case SMS_ITEM_COIN: popup(&at, "+1", SMS_RGB(255, 220, 60)); break;
	case SMS_ITEM_RED_COIN: popup(&at, "RED!", SMS_RGB(255, 80, 70)); break;
	case SMS_ITEM_BLUE_COIN: popup(&at, "BLUE!", SMS_RGB(90, 160, 255)); break;
	default: popup(&at, "GOT IT", SMS_RGB(255, 255, 255)); break;
	}
	return SMS_CONTINUE;
}

static int onShine(SmsMod* mod, SmsEvent* ev, void* user)
{
	(void)mod;
	(void)user;
	const SmsEvShine* s = (const SmsEvShine*)ev->data;
	char text[64];
	snprintf(text, sizeof text, "Shine #%d!  %d on this file", s->shine, s->total);
	banner(text, 5.0);
	api->core->log(self, SMS_LOG_INFO, "Shine %d collected (%d in all)", s->shine, s->total);
	return SMS_CONTINUE;
}

static int onDamage(SmsMod* mod, SmsEvent* ev, void* user)
{
	(void)mod;
	(void)ev;
	(void)user;
	return OPT("invincible", 0) ? SMS_CANCEL : SMS_CONTINUE;
}

static const float kFov[] = {0, 40, 50, 60, 70, 80};
static int onCamera(SmsMod* mod, SmsEvent* ev, void* user)
{
	(void)mod;
	(void)user;
	SmsEvCamera* c = (SmsEvCamera*)ev->data;
	const int i    = OPT("fov", 0);
	if (i > 0 && i < (int)(sizeof kFov / sizeof kFov[0]))
		c->fovy = kFov[i];
	return SMS_CONTINUE;
}

static int onKey(SmsMod* mod, SmsEvent* ev, void* user)
{
	(void)mod;
	(void)user;
	const SmsEvKey* k = (const SmsEvKey*)ev->data;
	if (k->scancode == s_toggleKey) {
		if (k->down && !k->repeat) {
			s_panel = !s_panel;
			api->config->set_int(self, "panel", s_panel);
		}
		return SMS_CANCEL; /* the game never sees it */
	}
	return SMS_CONTINUE;
}

/* another mod's event, if it is there: stage-timer's splits */
typedef struct Split {
	float seconds;
	int shine;
} Split;

static int onSplit(SmsMod* mod, SmsEvent* ev, void* user)
{
	(void)mod;
	(void)user;
	if (ev->size < sizeof(Split))
		return SMS_CONTINUE;
	const Split* s = (const Split*)ev->data;
	SmsPlayer p;
	p.size = sizeof p;
	if (api->player->get(&p)) {
		char text[32];
		snprintf(text, sizeof text, "SPLIT %d:%05.2f", (int)(s->seconds / 60), fmodf(s->seconds, 60.0f));
		SmsVec3 at = p.position;
		at.y += 220;
		popup(&at, text, SMS_RGB(120, 255, 160));
	}
	return SMS_CONTINUE;
}

/* ------------------------------------------------------------------ drawing */

static void drawPanel(const SmsDrawApi* d)
{
	SmsPlayer p;
	p.size = sizeof p;
	if (!s_panel || !OPT("show_panel", 1) || !api->player->get(&p))
		return;
	SmsRect r;
	if (!d->hud_slot(self, SMS_ANCHOR_TOP_LEFT, 176, 96, &r))
		return;
	d->rect_gradient(r.x, r.y, r.w, r.h, SMS_RGBA(10, 20, 40, 200), SMS_RGBA(10, 10, 20, 170));
	d->rect(r.x, r.y, 3, r.h, SMS_RGB(250, 190, 40));
	char line[96];
	const SmsColor white = SMS_RGB(255, 255, 255), dim = SMS_RGB(170, 180, 200);
	float y = r.y + 5;
	snprintf(line, sizeof line, "SPEED %6.0f", s_smoothSpeed);
	d->text(r.x + 8, y, 12, white, SMS_TEXT_SHADOW, line);
	y += 13;
	snprintf(line, sizeof line, "X %7.0f  Y %6.0f  Z %7.0f", p.position.x, p.position.y, p.position.z);
	d->text(r.x + 8, y, 10, dim, 0, line);
	y += 11;
	snprintf(line, sizeof line, "HP %d/%d  ANGLE %3.0f  %s%s", p.health, p.max_health,
	         p.yaw < 0 ? p.yaw + 360 : p.yaw, p.on_ground ? "GROUND" : "AIR", p.in_water ? " WATER" : "");
	d->text(r.x + 8, y, 10, dim, 0, line);
	y += 11;
	/* FLUDD's tank as a bar, with the drop icon */
	if (p.has_fludd && p.max_water > 0) {
		d->image(s_dropIcon, r.x + 8, y, 10, 9, SMS_RGB(255, 255, 255));
		const float k = (float)p.water / (float)p.max_water;
		d->rect(r.x + 20, y + 2, 100, 5, SMS_RGBA(255, 255, 255, 40));
		d->rect(r.x + 20, y + 2, 100 * k, 5, SMS_RGB(70, 170, 255));
		snprintf(line, sizeof line, "%3.0f%%", k * 100);
		d->text(r.x + 126, y, 10, dim, 0, line);
	}
	y += 12;
	/* the speed graph */
	const float gx = r.x + 8, gy = y, gw = r.w - 16, gh = r.y + r.h - y - 5;
	d->rect(gx, gy, gw, gh, SMS_RGBA(0, 0, 0, 90));
	float top = 400;
	for (int i = 0; i < GRAPH_N; i++)
		if (s_speed[i] > top)
			top = s_speed[i];
	d->clip_push(gx, gy, gw, gh);
	for (int i = 1; i < GRAPH_N; i++) {
		const float a = s_speed[(s_speedHead + i - 1) % GRAPH_N], b = s_speed[(s_speedHead + i) % GRAPH_N];
		d->line(gx + gw * (i - 1) / (GRAPH_N - 1), gy + gh - gh * a / top, gx + gw * i / (GRAPH_N - 1),
		        gy + gh - gh * b / top, 1.2f, SMS_RGB(250, 190, 40));
	}
	d->clip_pop();
	d->image(s_logo, r.x + r.w - 20, r.y + 4, 16, 16, SMS_RGBA(255, 255, 255, 220));
}

static void drawWorld(const SmsDrawApi* d)
{
	SmsPlayer p;
	p.size = sizeof p;
	if (!api->player->get(&p))
		return;
	if (OPT("trail", 1) && s_trailCount > 1) {
		for (int i = 1; i < s_trailCount; i++) {
			const int a = (s_trailHead - s_trailCount + i - 1 + TRAIL_MAX) % TRAIL_MAX;
			const int b = (a + 1) % TRAIL_MAX;
			const int alpha = 40 + 200 * i / s_trailCount;
			d->world_line(&s_trail[a], &s_trail[b], 2.0f, SMS_RGBA(80, 200, 255, alpha));
		}
	}
	/* where he lands */
	if (OPT("landing_ring", 1) && !p.on_ground && p.position.y - p.floor.y > 40) {
		SmsVec3 at = {p.position.x, p.floor.y + 2, p.position.z};
		d->world_ring(&at, 45, 2.0f, SMS_RGBA(255, 255, 255, 200));
		d->world_line(&p.position, &at, 1.0f, SMS_RGBA(255, 255, 255, 90));
	}
	/* the pop-ups rise and fade over a second */
	const double now = api->core->time();
	for (int i = 0; i < POPUPS; i++) {
		Popup* q        = &s_popups[i];
		const double age = now - q->born;
		if (!q->text[0] || age > 1.2)
			continue;
		SmsVec3 at = q->at;
		at.y += (float)age * 120.0f;
		const int alpha = (int)(255 * (age < 0.8 ? 1.0 : (1.2 - age) / 0.4));
		d->world_text(&at, 16, 1, (q->color & 0xFFFFFF00u) | (uint32_t)alpha, SMS_TEXT_OUTLINE, q->text);
	}
}

static int onDraw(SmsMod* mod, SmsEvent* ev, void* user)
{
	(void)mod;
	(void)user;
	const SmsEvDraw* e   = (const SmsEvDraw*)ev->data;
	const SmsDrawApi* d  = api->draw;
	SmsGameState st;
	st.size = sizeof st;
	api->game->state(&st);
	if (st.paused) /* out of the pause menu's way */
		return SMS_CONTINUE;
	if (e->mode == SMS_MODE_STAGE) {
		drawWorld(d);
		drawPanel(d);
	}
	if (api->core->time() < s_bannerUntil) {
		SmsRect r;
		const float w = d->text_width(14, s_banner) + 24;
		if (d->hud_slot(self, SMS_ANCHOR_TOP, w, 22, &r)) {
			d->rect(r.x, r.y, r.w, r.h, SMS_RGBA(0, 0, 0, 150));
			d->rect_outline(r.x, r.y, r.w, r.h, 1, SMS_RGBA(250, 190, 40, 200));
			d->text(r.x + r.w / 2, r.y + 5, 14, SMS_RGB(255, 255, 255), SMS_TEXT_CENTER | SMS_TEXT_SHADOW, s_banner);
		}
	}
	return SMS_CONTINUE;
}

/* ------------------------------------------------------------------ loading */

static void onToggleChanged(SmsMod* mod, const char* key, void* user)
{
	(void)user;
	api->core->log(mod, SMS_LOG_DEBUG, "%s is now %d", key, api->config->get_int(mod, key, 0));
}

static void addBool(const char* key, const char* label, int def)
{
	SmsOption o;
	memset(&o, 0, sizeof o);
	o.size      = sizeof o;
	o.key       = key;
	o.label     = label;
	o.type      = SMS_OPTION_BOOL;
	o.def       = (float)def;
	o.on_change = onToggleChanged;
	api->config->add_option(self, &o);
}

/* A 16 x 16 drop of water, drawn into pixels. */
static SmsImage makeDropIcon(void)
{
	uint8_t px[16 * 16 * 4];
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++) {
			const float fx = (x - 7.5f) / 6.0f, fy = (y - 10.0f) / 6.0f;
			/* a circle below, a point above */
			const int in = fx * fx + fy * fy < 1.0f || (y < 10 && fabsf(fx) < (float)(y - 1) / 9.0f);
			uint8_t* p   = px + (y * 16 + x) * 4;
			p[0]         = (uint8_t)(90 + x * 6);
			p[1]         = (uint8_t)(170 + y * 4);
			p[2]         = 255;
			p[3]         = in ? 255 : 0;
		}
	return api->draw->image_create(self, px, 16, 16);
}

SMS_MOD_DEFINE()

SMS_MOD_EXTERN_C SMS_MOD_EXPORT int sms_mod_load(SmsMod* mod, const SmsApi* a)
{
	api  = a;
	self = mod;
	if (!SMS_API_HAS(api->draw, hud_slot))
		return 1; /* the game is older than this mod */
	const SmsCoreApi* c = api->core;
	c->subscribe(mod, SMS_EV_STAGE_LOADED, onStage, NULL, SMS_PRIORITY_NORMAL);
	c->subscribe(mod, SMS_EV_TICK, onTick, NULL, SMS_PRIORITY_NORMAL);
	c->subscribe(mod, SMS_EV_ITEM_COLLECTED, onItem, NULL, SMS_PRIORITY_NORMAL);
	c->subscribe(mod, SMS_EV_SHINE_COLLECTED, onShine, NULL, SMS_PRIORITY_NORMAL);
	c->subscribe(mod, SMS_EV_PLAYER_DAMAGE, onDamage, NULL, SMS_PRIORITY_NORMAL);
	c->subscribe(mod, SMS_EV_CAMERA, onCamera, NULL, SMS_PRIORITY_NORMAL);
	c->subscribe(mod, SMS_EV_KEY, onKey, NULL, SMS_PRIORITY_NORMAL);
	c->subscribe(mod, SMS_EV_DRAW, onDraw, NULL, SMS_PRIORITY_NORMAL);
	s_splitEvent = c->event_id("stage-timer.split");
	c->subscribe(mod, s_splitEvent, onSplit, NULL, SMS_PRIORITY_NORMAL);

	s_toggleKey = api->input->bind_key(mod, "toggle_panel", "F3");
	s_moonKey   = api->input->bind_key(mod, "moon_jump", "M");
	s_panel     = api->config->get_int(mod, "panel", 1);

	addBool("show_panel", "Speed panel (F3)", 1);
	addBool("trail", "Trail behind Mario", 1);
	addBool("landing_ring", "Landing ring", 1);
	addBool("coin_popups", "Coin pop-ups", 1);
	static const char* const fovs[] = {"Game's", "40", "50", "60", "70", "80"};
	SmsOption fov;
	memset(&fov, 0, sizeof fov);
	fov.size         = sizeof fov;
	fov.key          = "fov";
	fov.label        = "Field of view";
	fov.type         = SMS_OPTION_CHOICE;
	fov.choices      = fovs;
	fov.choice_count = 6;
	api->config->add_option(mod, &fov);
	addBool("moon_jump", "Moon jump (hold M)", 0);
	addBool("invincible", "Invincible", 0);

	s_dropIcon = makeDropIcon();
	s_logo     = api->draw->image_load(mod, "logo.png");
	c->log(mod, SMS_LOG_INFO, "ready: F3 shows the panel, F1 has the settings (game %s)", c->game_build());
	return 0;
}

SMS_MOD_EXTERN_C SMS_MOD_EXPORT void sms_mod_unload(SmsMod* mod)
{
	api->core->log(mod, SMS_LOG_INFO, "bye");
}
