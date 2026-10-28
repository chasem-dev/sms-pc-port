// Characters (src/port_include/port_character.h): archives in mods/characters/
// that take the place of Mario's own. Each <id>.szs there is a whole
// /data/mario.szs (Yaz0 or plain RARC) with the same files: ma_mdl1.bmd on
// Mario's skeleton, the hand and cap models, and the animations. An optional
// <id>.txt beside it holds the name to show; otherwise the id is shown with a
// capital. The list, the choice and the settings menu's option are here; the
// game mounts the chosen archive as each stage loads Mario and reloads the
// area for CHARACTER_NEXT (patch zzzzzz-character-01).
#include "port_character.h"
#include "port_platform.h"
#include "port_settings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <strings.h>
#include <sys/stat.h>
#include <vector>

namespace {

struct Character {
	std::string id, name, path;
};

std::vector<Character> g_list;
bool g_listed;
int g_selected;
int g_available = 1;

bool is_file(const std::string& p)
{
	struct stat st;
	return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string display_name(const std::string& dir, const std::string& id)
{
	if (FILE* f = fopen((dir + id + ".txt").c_str(), "r")) {
		char line[64] = {};
		const bool got = fgets(line, sizeof line, f) != nullptr;
		fclose(f);
		std::string s = got ? line : "";
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
			s.pop_back();
		if (!s.empty())
			return s;
	}
	std::string s = id;
	for (char& c : s)
		if (c == '_' || c == '-')
			c = ' ';
	if (!s.empty())
		s[0] = (char)toupper((unsigned char)s[0]);
	return s;
}

void list()
{
	if (g_listed)
		return;
	g_listed = true;
	g_list.push_back({ "mario", "Mario", "" });
	for (const char* base : { "mods/characters/", "../../mods/characters/" }) {
		DIR* d = opendir(base);
		if (!d)
			continue;
		std::vector<Character> found;
		while (dirent* e = readdir(d)) {
			std::string f = e->d_name;
			if (f.size() <= 4 || f.compare(f.size() - 4, 4, ".szs") != 0 || !is_file(base + f))
				continue;
			const std::string id = f.substr(0, f.size() - 4);
			if (id == "mario")
				continue; // Mario is the disc's archive, or a game-file mod's
			found.push_back({ id, display_name(base, id), base + f });
		}
		closedir(d);
		std::sort(found.begin(), found.end(), [](const Character& a, const Character& b) { return a.id < b.id; });
		g_list.insert(g_list.end(), found.begin(), found.end());
		break; // the first folder that exists
	}
	if (const char* want = getenv("SMS_CHARACTER")) {
		// the id, or the name the settings menu shows (SMS Launcher keeps the menu's choice by name)
		bool found = false;
		for (size_t i = 0; i < g_list.size() && !found; i++)
			if (!strcasecmp(g_list[i].id.c_str(), want) || !strcasecmp(g_list[i].name.c_str(), want)) {
				g_selected = (int)i;
				found      = true;
			}
		if (!found)
			port_log("[character] %s: not in mods/characters; playing as Mario\n", want);
	}
	if (g_list.size() > 1)
		port_log("[character] %d character(s) in mods/characters; playing as %s\n", (int)g_list.size() - 1,
		         g_list[g_selected].name.c_str());
}

// The settings menu's option (Gameplay page). The change counts from the next
// area, as frame rate's does; CHARACTER_NEXT reloads the area at once.
int option_available() { return port_character_available() && port_character_count() > 1; }
int option_count() { return port_character_count(); }
const char* option_name(int i) { return port_character_name(i); }
int option_get() { return port_character_selected(); }
int option_set(int i)
{
	if (i < 0 || i >= port_character_count())
		return 0;
	port_character_select(i);
	return 1;
}

const PortSetting kSettings[] = {
	{ "character", PORT_SETTINGS_PAGE_GAMEPLAY, 90, "Character",
	  "Who you play as, from the next area. F4 switches at once.", 1, option_available,
	  option_count, option_name, option_get, option_set },
};

} // namespace

extern "C" {

int port_character_presses = 0;

int port_character_count(void)
{
	list();
	return (int)g_list.size();
}

const char* port_character_name(int index)
{
	list();
	return index >= 0 && index < (int)g_list.size() ? g_list[index].name.c_str() : "";
}

const char* port_character_path(int index)
{
	list();
	return index > 0 && index < (int)g_list.size() ? g_list[index].path.c_str() : nullptr;
}

int port_character_selected(void)
{
	list();
	return g_selected;
}

void port_character_select(int index)
{
	list();
	if (index < 0 || index >= (int)g_list.size() || index == g_selected)
		return;
	g_selected = index;
	port_log("[character] %s\n", g_list[index].name.c_str());
}

void port_character_set_available(int available) { g_available = available != 0; }
int port_character_available(void) { return g_available; }

const PortSetting* port_character_settings(int* count)
{
	*count = (int)(sizeof kSettings / sizeof kSettings[0]);
	return kSettings;
}

} // extern "C"
