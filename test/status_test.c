/* Status decode test (docs/07-status-decode.md): real A0A8 payloads from evidence/ must decode to "paper present, no error";
 * each decoded bit is then flipped individually and must produce exactly the documented condition. */
#include "../core/capt.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void mk(struct capt_status *s, unsigned s0, unsigned s4, unsigned s5, unsigned s6)   /* payload words S0, S4(+8), S5(+10), S6(+12) */
{
	memset(s, 0, sizeof *s);
	capt_put16(s->raw, s0); capt_put16(s->raw + 4, 0x000f); capt_put16(s->raw + 8, s4); capt_put16(s->raw + 10, s5); capt_put16(s->raw + 12, s6);
	s->st[0] = s0; s->st[1] = s4; s->st[2] = s5; s->st[3] = s6;
}

int main(void)
{
	struct capt_status s;
	/* real payloads (probe-001: asleep 8e33/S6 8000; capture-002/-003/-006/-007: idle 0001, receiving 0081, printing 0085, ejecting 4085; S6 0080) */
	mk(&s, 0x8e33, 0x0000, 0x0080, 0x8000); CHECK(capt_status_conditions(&s) == 0); CHECK(capt_status_summary(&s) == CAPT_ST_READY);
	mk(&s, 0x8c31, 0x0001, 0x0080, 0x0000); CHECK(capt_status_conditions(&s) == 0); CHECK(capt_status_summary(&s) == CAPT_ST_READY);
	mk(&s, 0x8c00, 0x0081, 0x0080, 0x0000); CHECK(capt_status_conditions(&s) == 0); CHECK(capt_status_summary(&s) == CAPT_ST_BUSY);
	mk(&s, 0x8c01, 0x0085, 0x0080, 0x0000); CHECK(capt_status_conditions(&s) == 0); CHECK(capt_status_summary(&s) == CAPT_ST_PRINTING);
	mk(&s, 0x0c13, 0x0081, 0x0080, 0x0080); CHECK(capt_status_conditions(&s) == 0);              /* S6 bit7 = misprint-query flag, not an error */
	/* one decoded bit at a time */
	mk(&s, 0x8c11, 0x0001, 0x0000, 0x0000); CHECK(capt_status_conditions(&s) == CAPT_C_NO_PAPER); CHECK(capt_status_summary(&s) == CAPT_ST_NO_PAPER);
	mk(&s, 0x8c11, 0x0001, 0x0080, 0x4000); CHECK(capt_status_conditions(&s) == CAPT_C_DOOR_OPEN); CHECK(capt_status_summary(&s) == CAPT_ST_ERROR);
	mk(&s, 0x8c11, 0x0001, 0x0080, 0x0100); CHECK(capt_status_conditions(&s) == CAPT_C_JAM);       CHECK(capt_status_summary(&s) == CAPT_ST_ERROR);
	mk(&s, 0x8c11, 0x0001, 0x0080, 0x0002); CHECK(capt_status_conditions(&s) == CAPT_C_SERVICE);   CHECK(capt_status_summary(&s) == CAPT_ST_ERROR);
	mk(&s, 0x8c11, 0x8001, 0x0080, 0x0000); CHECK(capt_status_conditions(&s) == CAPT_C_SERVICE);
	mk(&s, 0x8c11, 0x0001, 0x0080, 0x0004); CHECK(capt_status_conditions(&s) == 0);                /* S6 bit2 = "printing" in Canon, not an error */
	mk(&s, 0x8c11, 0x0001, 0x0080, 0x1000); CHECK(capt_status_conditions(&s) == 0);                /* S6 bit12 = "waiting" */
	{	struct capt_cartridge c[2]; memset(c, 0, sizeof c);
		CHECK(capt_cartridge_conditions(c, 1) == 0);                                  /* real probe: flag 0, id 0, flag2 0 */
		c[0].flag = 0x80; CHECK(capt_cartridge_conditions(c, 1) == CAPT_C_NO_CARTRIDGE);
		c[0].flag = 0x40; CHECK(capt_cartridge_conditions(c, 1) == CAPT_C_TONER_MEM);
		c[0].flag = 0x02; CHECK(capt_cartridge_conditions(c, 1) == CAPT_C_TONER_LOW);
		c[0].flag = 0x03; CHECK(capt_cartridge_conditions(c, 1) == 0);               /* flag 0x01 outranks 0x02 in FUN_080c5508 */
		c[0].flag = 0x80; c[0].id = 0x10; CHECK(capt_cartridge_conditions(c, 1) == 0); /* drum ids are not toner slots */
	}
	if (fails) { printf("status_test: %d FAILED\n", fails); return 1; }
	printf("status_test: all checks passed\n");
	return 0;
}
