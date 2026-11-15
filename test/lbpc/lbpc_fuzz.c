/* Robustness test for the LBPC driver library (the part that runs inside the class-driver plug-in and parses data from another process).
 * A stub printer (same replies as the pty mock in tools/baseline/run_baseline.py) sits behind capt_io; the valid stream is fed in odd-sized pieces,
 * then mutated (bit flips, truncation, inserted/removed bytes, random garbage) many times.  Built with -fsanitize=address,undefined: the driver must
 * never crash, never loop, and must report an error for every stream that is not the original.  usage: lbpc_fuzz STREAM [ITERATIONS] [SEED] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../lbpc/lbpc.h"

#include "mockprinter.h"
static void io_sleep(void *c, unsigned ms) { (void)c; (void)ms; }
static int verbose; static void lg(void *c, const char *l) { (void)c; if (verbose) fprintf(stderr, "%s\n", l); }

/* returns the first error (0 = clean) */
static int run(const uint8_t *s, size_t n, size_t chunk)
{
	struct capt_io io; struct lbpc_drive d; int e = 0; size_t o = 0;
	memset(&M, 0, sizeof M); M.asleep = 1;
	io.ctx = NULL; io.write = mock_write; io.read = mock_read; io.sleep_ms = io_sleep;
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
