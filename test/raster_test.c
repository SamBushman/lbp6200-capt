/* End-to-end: real Canon wire band stream -> Canon decoder -> bitmap -> OUR core (capt_job_page_lines, hiscoa)
 * -> mock printer collects C0A0 payload -> Canon decoder -> must equal the original bitmap. (needs -m32 + Canon libs) */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/capt.h"
typedef int (*dec_fn)(int *srcptr, int dst, int linesize, int nlines, int *srclen, int zero, unsigned char *params);
static unsigned char *img; static const int LS = 592, TOTAL = 6778;
static unsigned char got[400000]; static size_t gotn; static int c0a0_pkts, other; static unsigned lastcmd;
static int m_write(void *c, const uint8_t *b, size_t n)
{ (void)c; lastcmd = capt_get16(b); if (capt_get16(b) == IC_VIDEO_DATA) { memcpy(got + gotn, b + 4, n - 4); gotn += n - 4; c0a0_pkts++; } else other++; return 0; }
static int m_read(void *c, uint8_t *b, size_t n, int t)
{ (void)c; (void)t; uint8_t r[6] = { 0, 0, 6, 0, 0, 0 }; capt_put16(r, lastcmd); if (n > 6) n = 6; memcpy(b, r, n); return (int)n; }
static int fetch(void *c, uint8_t *dst, unsigned y) { (void)c; memcpy(dst, img + (size_t)LS * y, LS); return 0; }
int main(int argc, char **argv)
{
	void *h = dlopen(argv[1], RTLD_NOW); dec_fn dec = (dec_fn)dlsym(h, "lCaptDecode");
	FILE *f = fopen(argv[2], "rb"); static unsigned char in[200000]; int n = (int)fread(in, 1, sizeof in, f) - 4;
	unsigned char params[8] = { 1, 4, 1, 1, 0, 0xf0, 0, 0 };
	img = calloc(1, (size_t)LS * TOTAL);
	int src = (int)(size_t)(in + 4), sl = n, line = 0;
	while (line < TOTAL) {
		int nl = TOTAL - line < 256 ? TOTAL - line : 256; unsigned char *b = calloc(1, (size_t)LS * (nl + 16));
		if (dec(&src, (int)(size_t)(b + LS * 16), LS, nl, &sl, 0, params) != nl) { puts("source decode failed"); return 2; }
		memcpy(img + (size_t)LS * line, b + LS * 16, (size_t)LS * nl); free(b); line += nl;
	}
	struct capt_io io = { 0, m_write, m_read, 0 }; struct capt_job j; struct capt_page_opts o;
	static const uint8_t when[8] = { 0xea, 7, 0xb, 0xd, 0x17, 6, 0x2e, 3 };
	capt_page_opts_default(&o);
	j.io = &io; memcpy(j.when, when, 8); strcpy(j.name, "t"); j.reserve_id = 2;
	int e = capt_job_page_lines(&j, &o, fetch, NULL);
	printf("page_lines rc=%d, %d C0A0 packets, %zu band bytes (wire reference %d)\n", e, c0a0_pkts, gotn, n);
	src = (int)(size_t)got; sl = (int)gotn; line = 0; int ok = 1;
	while (line < TOTAL) {
		int nl = TOTAL - line < 256 ? TOTAL - line : 256; unsigned char *b = calloc(1, (size_t)LS * (nl + 16));
		int r = dec(&src, (int)(size_t)(b + LS * 16), LS, nl, &sl, 0, params);
		if (r != nl || memcmp(b + LS * 16, img + (size_t)LS * line, (size_t)LS * nl)) { printf("band at line %d: MISMATCH (ret %d)\n", line, r); ok = 0; break; }
		free(b); line += nl;
	}
	printf("%s: %d lines decoded by Canon's decoder from OUR stream\n", ok ? "PASS identical image" : "FAIL", line);
	return !ok || e;
}
