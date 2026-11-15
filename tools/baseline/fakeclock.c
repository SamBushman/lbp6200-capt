/* LD_PRELOAD shim for reproducible filter runs: time() starts at a fixed instant and then advances with real time
 * (so the core's timeout loops still work); gethostname() returns a constant.  Build: cc -shared -fPIC -o fakeclock.so fakeclock.c -ldl */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define FAKE_START 1794657600L   /* 2026-11-14 12:00:00 UTC */
time_t time(time_t *t)
{
	static time_t (*real)(time_t *); static time_t base;
	if (!real) real = (time_t (*)(time_t *))dlsym(RTLD_NEXT, "time");
	time_t now = real(NULL);
	if (!base) base = now;
	time_t v = FAKE_START + (now - base);
	if (t) *t = v;
	return v;
}
int gethostname(char *name, size_t len)
{
	strncpy(name, "baselinehost", len);
	if (len) name[len - 1] = 0;
	return 0;
}
