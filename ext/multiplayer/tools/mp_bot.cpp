// A stand-in player for testing multiplayer without more games running
// (ext/multiplayer/README.md#testing): joins a host through the same
// controller the game uses, follows it from stage to stage, and stands
// beside the host doing whatever the host's Mario does (its pose, moved
// sideways), so the host's game shows it as a puppet.
//
//   mp_bot ADDRESS[:PORT] [--name NAME] [--offset X,Z] [--seconds N] [--coins N] [--turn]
//
// --turn faces the other way from the host (towards the camera behind them).
// --coins N reports N coins picked up, one a second from three seconds in
// the host's stage, to check that pickups add up in the host's game.
#include "../mp_game_api.h"

#include <chrono>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <vector>

int main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: mp_bot ADDRESS[:PORT] [--name NAME] [--offset X,Z] [--seconds N] [--coins N]\n");
		return 2;
	}
	const char* name = "Bot";
	float offX = 150.0f, offZ = 0.0f;
	double seconds = 60;
	int coins      = 0;
	bool turn      = false;
	for (int i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "--name") && i + 1 < argc)
			name = argv[++i];
		else if (!strcmp(argv[i], "--offset") && i + 1 < argc)
			sscanf(argv[++i], "%f,%f", &offX, &offZ);
		else if (!strcmp(argv[i], "--seconds") && i + 1 < argc)
			seconds = atof(argv[++i]);
		else if (!strcmp(argv[i], "--coins") && i + 1 < argc)
			coins = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--turn"))
			turn = true;
	}
	setenv("SMS_MULTIPLAYER", "join", 1);
	setenv("SMS_MULTIPLAYER_ADDRESS", argv[1], 1);
	setenv("SMS_MULTIPLAYER_NAME", name, 1);
	if (mpg_start() != MPG_ROLE_CLIENT)
		return 1;

	std::vector<int32_t> flags(mpg_flag_count(), 0);
	int coinIndex = -1;
	for (int i = 0; i < mpg_flag_count(); i++)
		if (mpg_flag_id(i) == 0x40002)
			coinIndex = i;
	const auto start = std::chrono::steady_clock::now();
	int frame = 0, given = 0, inStage = 0;
	for (;;) {
		const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		if (t > seconds)
			break;
		MpgGame g;
		memset(&g, 0, sizeof g);
		int area, episode;
		// "in" the host's stage as soon as it is known
		if (mpg_target(&area, &episode) && mpg_flags_ready()) {
			g.playing = 1;
			g.area    = area;
			g.episode = episode;
		}
		mpg_frame(&g);
		if (g.playing) {
			mpg_flags_sync(flags.data());
			inStage++;
			if (coins && coinIndex >= 0 && given < coins && inStage > 90 + given * 30) {
				flags[coinIndex]++;
				given++;
				fprintf(stderr, "[bot %s] picked up a coin (%d)\n", name, given);
			}
		}
		MpgPose host;
		float age;
		MpgPose mine;
		memset(&mine, 0, sizeof mine);
		mine.session = mpg_session();
		if (mpg_pose(0, &host, &age)) {
			mine = host;
			// beside the host, in the host's own frame (to its right)
			mine.mtx[0][3] += host.mtx[0][0] * offX + host.mtx[0][2] * offZ;
			mine.mtx[1][3] += host.mtx[1][0] * offX + host.mtx[1][2] * offZ;
			mine.mtx[2][3] += host.mtx[2][0] * offX + host.mtx[2][2] * offZ;
			if (turn)
				for (int r = 0; r < 3; r++) {
					mine.mtx[r][0] = -mine.mtx[r][0];
					mine.mtx[r][2] = -mine.mtx[r][2];
				}
		}
		mpg_set_pose(&mine);
		char line[256];
		while (mpg_take_notice(line, sizeof line))
			fprintf(stderr, "[bot %s] %s\n", name, line);
		if (getenv("MP_BOT_DEBUG") && frame % 60 == 0 && mpg_pose(0, &host, &age))
			fprintf(stderr, "[bot %s] host pose: flags %02x hand %d nozzle %d btp %d body %d@%.1f upper %d@%.1f fludd %d@%.1f yoshi %d@%.1f\n",
			        name, host.flags, host.hand, host.nozzle, host.btp, host.bodyBck, host.bodyFrame, host.upperBck,
			        host.upperFrame, host.fluddBck, host.fluddFrame, host.yoshiBck, host.yoshiFrame);
		if (++frame % 300 == 0) {
			char status[512];
			mpg_status(status, sizeof status);
			fprintf(stderr, "[bot %s] session %u\n%s\n", name, mpg_session(), status);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(33));
	}
	return 0;
}
