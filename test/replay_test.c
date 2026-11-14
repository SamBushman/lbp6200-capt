/* Replays our job engine against a mock printer and compares every TX frame
 * with the real capture (evidence/capture-002/job3-frames.txt, first job). */
#include "../core/capt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t last[CAPT_MAX_PKT]; static size_t lastn;
static uint8_t tx[64][CAPT_MAX_PKT]; static size_t txn[64]; static int ntx;
static uint8_t rep[16]; static size_t repn, repoff;

static int m_write(void *c, const uint8_t *b, size_t n)
{
	(void)c; memcpy(tx[ntx], b, n); txn[ntx++] = n;
	unsigned cmd = capt_get16(b);
	repoff = 0;
	if (cmd == PC_RESERVE_UNIT) { const uint8_t r[8] = { 0xa0,0xa2,8,0,0,0,2,0 }; memcpy(rep, r, 8); repn = 8; }
	else if (cmd >= 0xE000 && cmd < 0xF000 ) { capt_put16(rep, cmd); capt_put16(rep + 2, 6); capt_put16(rep + 4, 0); repn = 6; }
	else repn = 0;                      /* D0xx / C0xx: no reply */
	return 0;
}
static int m_read(void *c, uint8_t *b, size_t n, int t)
{
	(void)c; (void)t;
	size_t k = repn - repoff; if (k > n) k = n;
	memcpy(b, rep + repoff, k); repoff += k; return (int)k;
}
static int hex(const char *s, uint8_t *o)
{ int n = 0; for (; s[0] && s[1] && s[0] != '\n'; s += 2) { unsigned v; sscanf(s, "%2x", &v); o[n++] = (uint8_t)v; } return n; }

int main(int argc, char **argv)
{
	const char *frames = argc > 1 ? argv[1] : "../../evidence/capture-002/job3-frames.txt";
	const char *capt = argc > 2 ? argv[2] : "../../evidence/capture-002/wire-c0a0.bin";
	struct capt_io io = { 0, m_write, m_read, 0 };
	struct capt_job j; struct capt_page_opts o;
	/* band payload = the real wire C0A0 (evidence/capture-002/wire-c0a0.bin, 4-byte header + band) */
	FILE *f = fopen(capt, "rb"); static uint8_t cb[200000]; size_t cn = f ? fread(cb, 1, sizeof cb, f) : 0;
	if (!f) { perror(capt); return 2; }
	fclose(f);
	const uint8_t *band = cb + 4; size_t bl = cn - 4;
	printf("band payload %zu bytes\n", bl);
	static const uint8_t when[8] = { 0xea,0x07,0x0b,0x0d,0x17,0x06,0x2e,0x03 };
	capt_page_opts_default(&o);
	if (capt_job_begin(&j, &io, when, "bazzitesamtestpage.ps")) { puts("begin failed"); return 1; }
	const uint8_t *bands[1] = { band }; size_t lens[1] = { bl };
	if (capt_job_page(&j, &o, bands, lens, 1)) { puts("page failed"); return 1; }
	capt_job_end(&j);

	/* compare against capture (first job = frames until E0A9) */
	FILE *g = fopen(frames, "r"); char line[400000]; int k = 0, bad = 0;
	while (fgets(line, sizeof line, g) && k < ntx) {
		char t[64], dir[8], cmd[8], sz[32], hx[400000];
		if (sscanf(line, "%63s %7s %7s %31s %399999s", t, dir, cmd, sz, hx) < 5) continue;
		static uint8_t exp[CAPT_MAX_PKT]; int en = hex(hx, exp);
		int full = strstr(sz, "size=") && !strstr(line, "(first");
		size_t cmpn = full ? (size_t)en : (size_t)en;       /* C0A0 line only has first 96 bytes */
		if (strstr(line, "(first")) { if (txn[k] == cn) { if (memcmp(tx[k], cb, cn)) { printf("MISMATCH %s (full)\n", cmd); bad++; } else printf("ok       %s (FULL %zu bytes)\n", cmd, cn); } else if (memcmp(tx[k], exp, cmpn)) { printf("MISMATCH %s (prefix)\n", cmd); bad++; } else printf("ok       %s (prefix of %zu)\n", cmd, txn[k]); }
		else if ((size_t)en != txn[k] || memcmp(tx[k], exp, en)) {
			printf("MISMATCH %s: exp %d got %zu\n", cmd, en, txn[k]);
			for (int i = 0; i < en && (size_t)i < txn[k]; i++) if (tx[k][i] != exp[i]) { printf("   first diff @%d exp %02x got %02x\n", i, exp[i], tx[k][i]); break; }
			bad++;
		} else printf("ok       %s\n", cmd);
		k++;
		if (!strcmp(cmd, "E0A9")) break;
	}
	printf("%d frames compared, %d mismatches, mock saw %d frames\n", k, bad, ntx);
	return bad != 0;
}
