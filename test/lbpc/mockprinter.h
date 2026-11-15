/* Stub printer for tests (same replies as the pty mock in tools/baseline/run_baseline.py).  Header-only; include after lbpc.h/capt.h.
 * mock_write(): accepts one or several CAPT frames; mock_read(): returns queued reply bytes (0 = nothing).  M.frames counts frames.
 * If `mock_wire` is set, every received frame is appended to it (for comparison with the golden capture). */
#ifndef MOCKPRINTER_H
#define MOCKPRINTER_H
#include <stdio.h>
#include <string.h>
static FILE *mock_wire;
static struct { uint8_t q[512]; size_t qn, qo; unsigned starts, decs; int online, asleep; unsigned long frames; } M;
static void reply(unsigned c, const uint8_t *pl, size_t n) { size_t k = capt_pkt(M.q + M.qn, sizeof M.q - M.qn, c, pl, n); M.qn += k; }
static int frame(const uint8_t *b, size_t n)
{
	unsigned cmd;
	if (n < 4) return -1;
	cmd = capt_get16(b); M.frames++;
	if (mock_wire) fwrite(b, 1, n, mock_wire);
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
static int mock_write(void *c, const uint8_t *b, size_t n)
{
	(void)c;
	while (n) { size_t sz; if (n < 4 || (sz = capt_get16(b + 2)) < 4 || sz > n) return -1; if (frame(b, sz)) return -1; b += sz; n -= sz; }
	return 0;
}
static int mock_read(void *c, uint8_t *b, size_t n, int to)
{
	size_t k = M.qn - M.qo; (void)c; (void)to;
	if (!k) return 0;
	if (k > n) k = n;
	memcpy(b, M.q + M.qo, k); M.qo += k; if (M.qo == M.qn) M.qo = M.qn = 0;
	return (int)k;
}
#endif
