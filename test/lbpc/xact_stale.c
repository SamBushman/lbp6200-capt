/* Host unit test: a status query that meets stale replies of an earlier (killed) job in the pipe resynchronises; a non-query command is not repeated. */
#include <stdio.h>
#include <string.h>
#include "../../core/capt.h"
static uint8_t q[4096]; static size_t qn, qo; static int writes;
static void reply(unsigned c, size_t pl) { uint8_t p[256] = { 0 }; size_t k = capt_pkt(q + qn, sizeof q - qn, c, p, pl); qn += k; }
static int mw(void *c, const uint8_t *b, size_t n) { unsigned cmd = capt_get16(b); (void)c; writes++; if (cmd == 0xa0a8) reply(0xa0a8, 88); else if (cmd == 0xe0a7) reply(0xe0a7, 2); return 0; }
static int mr(void *c, uint8_t *b, size_t n, int to) { size_t k = qn - qo; (void)c; (void)to; if (k > n) k = n; memcpy(b, q + qo, k); qo += k; return (int)k; }
int main(void)
{
	struct capt_io io = { NULL, mw, mr, NULL }; uint8_t r[128]; int bad = 0, n;
	reply(0xa0a4, 16); reply(0xa0a8, 88); q[qn++] = 0x55; q[qn++] = 0x66;   /* stale: cartridge reply, status reply, 2 junk bytes */
	n = capt_xact(&io, 0xa0a8, NULL, 0, r, sizeof r, 100);
	if (n != 92 || capt_get16(r) != 0xa0a8) { printf("FAIL stale query -> %d\n", n); bad++; }
	qn = qo = 0; writes = 0; reply(0xa0a4, 16);
	n = capt_xact(&io, 0xe0a7, (const uint8_t *)"\1\0", 2, r, sizeof r, 100);
	if (n != -3 || writes != 1) { printf("FAIL non-query must not be repeated (n=%d writes=%d)\n", n, writes); bad++; }
	printf(bad ? "xact_stale: FAILED\n" : "xact_stale: OK\n"); return bad;
}
