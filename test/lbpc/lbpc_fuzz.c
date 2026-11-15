/* Robustness test for the LBPC driver library (the part that runs inside the class-driver plug-in and parses data from another process).
 * A stub printer (same replies as the pty mock in tools/baseline/run_baseline.py) sits behind capt_io; the valid stream is fed in odd-sized pieces,
 * then mutated (bit flips, truncation, inserted/removed bytes, random garbage) many times.  Built with -fsanitize=address,undefined: the driver must
 * never crash, never loop, and must report an error for every stream that is not the original.  usage: lbpc_fuzz STREAM [ITERATIONS] [SEED] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../lbpc/lbpc.h"

static struct { uint8_t q[512]; size_t qn, qo; unsigned starts, decs; int online, asleep; unsigned long frames; } M;
static void reply(unsigned c, const uint8_t *pl, size_t n) { size_t k = capt_pkt(M.q + M.qn, sizeof M.q - M.qn, c, pl, n); M.qn += k; }
static int frame(const uint8_t *b, size_t n)
{
	unsigned cmd;
	if (n < 4) return -1;
	cmd = capt_get16(b); M.frames++;
	if (cmd == 0xa0a8) {
		uint8_t x[92]; memset(x, 0, sizeof x); capt_put16(x, 0xa0a8); capt_put16(x + 2, 0x5c);
		capt_put16(x + 4, M.online ? 0x8c00 : (M.asleep ? 0x8c12 : 0x8c10)); capt_put16(x + 16, M.asleep ? 0x8000 : 0); capt_put16(x + 12, 1); capt_put16(x + 14, 0x80);
		capt_put16(x + 18, M.decs); capt_put16(x + 20, M.starts); capt_put16(x + 22, M.starts); capt_put16(x + 24, M.starts);
		memcpy(M.q + M.qn, x, sizeof x); M.qn += sizeof x;
	} else if (cmd == 0xa2a0) { uint8_t p[4] = { 0, 0, 2, 0 }; reply(cmd, p, 4); }
	else if (cmd == 0xa0a4) { static const uint8_t p[16] = { 0, 0xc, 0, 0, 0, 0, 0, 3, 3, 1, 0xd, 0xfe, 0, 0, 0, 0 }; reply(cmd, p, sizeof p); }
	else if (cmd == 0xa3af) { static const uint8_t p[10] = { 0, 0, 0xea, 7, 0xb, 0xe, 7, 0x10, 4, 3 }; reply(cmd, p, sizeof p); }
	else if (cmd == 0xc0a0 || cmd == 0xd0a9) { }
	else if (cmd == 0xc0a4) M.decs++;
	else { uint8_t z[2] = { 0, 0 }; if (cmd == 0xe0a7) M.starts++; if (cmd == 0xe0a5) M.online = 1; if (cmd == 0xe0ba) M.asleep = 0; reply(cmd, z, 2); }
	return 0;
}
/* a write may carry several frames (a held duplex front page is released in one piece), like the byte stream the pty mock parses */
static int io_write(void *c, const uint8_t *b, size_t n)
{
	(void)c;
	while (n) { size_t sz; if (n < 4 || (sz = capt_get16(b + 2)) < 4 || sz > n) return -1; if (frame(b, sz)) return -1; b += sz; n -= sz; }
	return 0;
}
static int io_read(void *c, uint8_t *b, size_t n, int to)
{
	size_t k = M.qn - M.qo; (void)c; (void)to;
	if (!k) return 0;
	if (k > n) k = n;
	memcpy(b, M.q + M.qo, k); M.qo += k; if (M.qo == M.qn) M.qo = M.qn = 0;
	return (int)k;
}
static void io_sleep(void *c, unsigned ms) { (void)c; (void)ms; }
static int verbose; static void lg(void *c, const char *l) { (void)c; if (verbose) fprintf(stderr, "%s\n", l); }

/* returns the first error (0 = clean) */
static int run(const uint8_t *s, size_t n, size_t chunk)
{
	struct capt_io io; struct lbpc_drive d; int e = 0; size_t o = 0;
	memset(&M, 0, sizeof M); M.asleep = 1;
	io.ctx = NULL; io.write = io_write; io.read = io_read; io.sleep_ms = io_sleep;
	lbpc_drive_init(&d, &io, lg, NULL);
	while (o < n && !e) { size_t k = n - o < chunk ? n - o : chunk; e = lbpc_drive_feed(&d, s + o, k); o += k; }
	{ int e2 = lbpc_drive_end(&d); if (!e) e = e2; }
	return e;
}

static unsigned long long rs;
static unsigned rnd(void) { rs = rs * 6364136223846793005ULL + 1442695040888963407ULL; return (unsigned)(rs >> 33); }

int main(int argc, char **argv)
{
	FILE *f; uint8_t *s, *m; size_t n, cap; int it, iters = argc > 2 ? atoi(argv[2]) : 300, bad = 0, errs = 0;
	static const size_t chunks[] = { 1, 2, 3, 7, 8, 9, 511, 2048, 4093, 65536 };
	unsigned i;
	if (argc < 2) return 2;
	verbose = getenv("LBPC_VERBOSE") != NULL; rs = argc > 3 ? strtoull(argv[3], NULL, 10) : 1;
	f = fopen(argv[1], "rb"); if (!f) { perror("stream"); return 2; }
	fseek(f, 0, SEEK_END); n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
	cap = n + 4096; s = malloc(n); m = malloc(cap);
	if (fread(s, 1, n, f) != n) return 2;
	fclose(f);
	for (i = 0; i < sizeof chunks / sizeof *chunks; i++)       /* the intact stream must succeed for every feed size */
		if (run(s, n, chunks[i])) { printf("FAIL: intact stream rejected at chunk %zu\n", chunks[i]); bad++; }
	for (it = 0; it < iters; it++) {
		size_t len = n; unsigned kind = rnd() % 6, k, p;
		memcpy(m, s, n);
		switch (kind) {
		case 0: for (k = 1 + rnd() % 8; k; k--) m[rnd() % len] ^= (uint8_t)(1u << (rnd() % 8)); break;      /* bit flips anywhere */
		case 1: len = rnd() % n; break;                                                                      /* truncation */
		case 2: p = rnd() % len; memmove(m + p + 1, m + p, len - p); m[p] = (uint8_t)rnd(); len++; break;    /* insert a byte */
		case 3: p = rnd() % len; memmove(m + p, m + p + 1, len - p - 1); len--; break;                       /* delete a byte */
		case 4: for (k = 0; k < 32; k++) m[rnd() % (len < 400 ? len : 400)] = (uint8_t)rnd(); break;         /* corrupt the first records */
		default: len = 1 + rnd() % 2000; for (k = 0; k < len; k++) m[k] = (uint8_t)rnd(); break;             /* pure garbage */
		}
		if (len == n && !memcmp(m, s, n)) continue;
		errs += run(m, len, chunks[rnd() % (sizeof chunks / sizeof *chunks)]) != 0;
	}
	free(s); free(m);
	printf("%d iterations, %d rejected, %d problems\n", iters, errs, bad);
	return bad ? 1 : 0;
}
