/* rand()/srand() with MSL's range and sequence (the ANSI C reference LCG),
 * used by game code through port_compat.h. */
static unsigned int s_next = 1;

int sms_msl_rand(void)
{
	s_next = s_next * 1103515245u + 12345u;
	return (int)((s_next >> 16) & 0x7FFF);
}

void sms_msl_srand(unsigned int seed) { s_next = seed; }
