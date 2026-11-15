/* Static-link variant of fakeclock.c for emulated (qemu-user) guests, where LD_PRELOAD does not apply: link this into the test binary. */
#include <string.h>
#include <time.h>
#include <unistd.h>
#define FAKE_START 1794657600L
time_t time(time_t *t)
{
	struct timespec ts; static time_t base; time_t v;
	clock_gettime(CLOCK_REALTIME, &ts);
	if (!base) base = ts.tv_sec;
	v = FAKE_START + (ts.tv_sec - base);
	if (t) *t = v;
	return v;
}
int gethostname(char *name, size_t len) { strncpy(name, "baselinehost", len); if (len) name[len - 1] = 0; return 0; }
