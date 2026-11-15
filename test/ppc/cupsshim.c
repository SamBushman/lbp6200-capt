/* Test-only stand-in for the few libcups entry points the LBP6200 filter uses, written against the CUPS 1.1 API (Mac OS X 10.4 headers).
 * Lets the UNMODIFIED filter sources run under qemu-ppc on the host (no PPC libcups there) and reads the neutral raster format made by
 * tools/baseline/ras2plain.c.  Not shipped. */
#include <cups/cups.h>
#include <cups/raster.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int rd(int fd, unsigned char *p, unsigned n)
{
	unsigned got = 0;
	while (got < n) { ssize_t k = read(fd, p + got, n - got); if (k <= 0) return -1; got += (unsigned)k; }
	return 0;
}
cups_raster_t *cupsRasterOpen(int fd, cups_mode_t mode)
{
	unsigned char m[4]; cups_raster_t *r;
	if (mode != CUPS_RASTER_READ || rd(fd, m, 4) || memcmp(m, "NRAS", 4)) return NULL;
	r = (cups_raster_t *)calloc(1, sizeof *r); r->fd = fd; r->mode = mode; r->sync = 0x4e524153;
	return r;
}
void cupsRasterClose(cups_raster_t *r) { free(r); }
unsigned cupsRasterReadHeader(cups_raster_t *r, cups_page_header_t *h)
{
	unsigned char b[36]; unsigned v[9], i;
	if (rd(r->fd, b, 36)) return 0;
	for (i = 0; i < 9; i++) v[i] = (unsigned)b[4*i] << 24 | (unsigned)b[4*i+1] << 16 | (unsigned)b[4*i+2] << 8 | b[4*i+3];
	memset(h, 0, sizeof *h);
	h->PageSize[0] = v[0]; h->PageSize[1] = v[1]; h->Duplex = v[2]; h->Tumble = v[3]; h->cupsWidth = v[4]; h->cupsHeight = v[5];
	h->cupsBitsPerPixel = v[6]; h->cupsBitsPerColor = v[6]; h->cupsBytesPerLine = v[7]; h->cupsColorSpace = v[8] == 3 ? CUPS_CSPACE_K : CUPS_CSPACE_W;
	return 1;
}
unsigned cupsRasterReadPixels(cups_raster_t *r, unsigned char *p, unsigned len) { return rd(r->fd, p, len) ? 0 : len; }

int cupsParseOptions(const char *arg, int n, cups_option_t **opts)
{
	char *s = strdup(arg), *tok, *sv = NULL;
	for (tok = strtok_r(s, " ", &sv); tok; tok = strtok_r(NULL, " ", &sv)) {
		char *eq = strchr(tok, '=');
		*opts = (cups_option_t *)realloc(*opts, (size_t)(n + 1) * sizeof **opts);
		(*opts)[n].name = strdup(tok); (*opts)[n].value = strdup(eq ? eq + 1 : "");
		if (eq) (*opts)[n].name[eq - tok] = 0;
		n++;
	}
	free(s);
	return n;
}
const char *cupsGetOption(const char *name, int n, cups_option_t *o)
{
	int i;
	for (i = 0; i < n; i++) if (!strcasecmp(o[i].name, name)) return o[i].value;
	return NULL;
}
