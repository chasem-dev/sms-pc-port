// Host entry point: sets up the platform layer, then enters the game's main().
#include <cstdio>
#include "port_platform.h"

extern void SMS_main(void);

int main(int argc, char** argv)
{
	port_init(argc, argv);
	std::fprintf(stderr, "[port] entering SMS_main\n");
	SMS_main();
	std::fprintf(stderr, "[port] SMS_main returned\n");
	return 0;
}
