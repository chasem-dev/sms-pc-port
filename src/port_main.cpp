// Host entry point: sets up the platform layer, then enters the game's main().
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "port_platform.h"

extern void SMS_main(void);

struct Args {
	int argc;
	char** argv;
};

static void* run_game(void* p)
{
	Args* a = (Args*)p;
	port_init(a->argc, a->argv);
	std::fprintf(stderr, "[port] entering SMS_main\n");
	SMS_main();
	std::fprintf(stderr, "[port] SMS_main returned\n");
	return NULL;
}

#if UINTPTR_MAX > 0xFFFFFFFFu
#include <pthread.h>

// 64-bit hosts: the game keeps pointers to its locals in u32 slots, so the
// boot thread (which becomes the default OSThread) runs on a stack below
// 2 GiB instead of the process's main stack.
int main(int argc, char** argv)
{
	const unsigned long size = 8ul << 20;
	void* stack = port_low_alloc(size);
	if (!stack) {
		std::fprintf(stderr, "[port] no memory below 2 GiB for the game's stack\n");
		return 1;
	}
	Args a = {argc, argv};
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstack(&attr, stack, size);
	pthread_t th;
	if (pthread_create(&th, &attr, run_game, &a) != 0) {
		std::fprintf(stderr, "[port] cannot start the game thread\n");
		return 1;
	}
	pthread_attr_destroy(&attr);
	pthread_join(th, NULL);
	return 0;
}
#else
int main(int argc, char** argv)
{
	Args a = {argc, argv};
	run_game(&a);
	return 0;
}
#endif
