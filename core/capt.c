#include "capt.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../hiscoa/hiscoa-compress.h"

volatile int *capt_cancel_flag = NULL;
#define CANCELLED() (capt_cancel_flag && *capt_cancel_flag)

size_t capt_pkt(uint8_t *out, size_t cap, unsigned cmd, const uint8_t *payload, size_t n)
{
	size_t tot = n + 4;
	if (tot > CAPT_MAX_PKT || tot > cap) return 0;
	capt_put16(out, cmd); capt_put16(out + 2, (unsigned)tot);
	if (n) memcpy(out + 4, payload, n);
	return tot;
}

static int read_full(struct capt_io *io, uint8_t *b, size_t n, int to)
{
	size_t got = 0;
	while (got < n) {
		int r = io->read(io->ctx, b + got, n - got, to);
		if (r < 0) return CAPT_EIO;
		if (r == 0) return CAPT_ETIMEOUT;
		got += (size_t)r;
	}
	return 0;
}

int capt_xact(struct capt_io *io, unsigned cmd, const uint8_t *payload, size_t n,
              uint8_t *reply, size_t cap, int to)
{
	uint8_t pk[CAPT_MAX_PKT];
	size_t len = capt_pkt(pk, sizeof pk, cmd, payload, n);
	if (!len) return CAPT_EARG;
	if (io->write(io->ctx, pk, len) < 0) return CAPT_EIO;
	if (!reply) return 0;
	if (cap < 6) return CAPT_EARG;
	int r = read_full(io, reply, 6, to);
	if (r) return r;
	unsigned rc = capt_get16(reply), rs = capt_get16(reply + 2);
	if (rc != cmd || rs < 6 || rs > cap) return CAPT_EPROTO;
	if (rs > 6 && (r = read_full(io, reply + 6, rs - 6, to)) != 0) return r;
	return (int)rs;
}

/* ------------------------------------------------------------ status */
int capt_get_status(struct capt_io *io, struct capt_status *s)
{
	uint8_t r[96];
	int n = capt_xact(io, PC_GET_EXT_STATUS2, NULL, 0, r, sizeof r, 2000);
	if (n < 0) return n;
	if (n < 0x5c) return CAPT_EPROTO;
	const uint8_t *p = r + 4;                 /* payload offsets as in docs/07 */
	memset(s, 0, sizeof *s);
	s->st[0] = capt_get16(p + 0);  s->st[1] = capt_get16(p + 8);  s->st[2] = capt_get16(p + 10);
	s->st[3] = capt_get16(p + 12); s->st[4] = capt_get16(p + 24); s->st[5] = capt_get16(p + 30);
	s->st[6] = capt_get16(p + 38);
	s->page_decoding = capt_get16(p + 14); s->page_printing = capt_get16(p + 16);
	s->page_out = capt_get16(p + 18);      s->page_completed = capt_get16(p + 20);
	s->job_no = capt_get16(p + 28);        s->eng_online_cmds = p[33];
	s->page_received = capt_get16(p + 34); s->pages_in_job = capt_get16(p + 40);
	s->pages_printed = capt_get16(p + 44); s->event_counter = capt_get16(p + 64);
	memcpy(s->raw, p, sizeof s->raw);
	return 0;
}

/* Byte-level decode of the A0A8 payload.  Canon's C_Port::GetStatus stores the reply at port+0x54d8, so payload offset = port offset - 0x54dc;
 * C_Port::CheckStatusSub (FUN_080c9...) turns the bytes into error bits (SetBit = FUN_0806809c) that the decision tree FUN_080bbd.. maps to
 * message ids.  Every line below names that chain (docs/07-status-decode.md).  Bits not listed are NOT reported (unproven). */
unsigned capt_status_conditions(const struct capt_status *s)
{
	const uint8_t *p = s->raw; unsigned c = 0;
	if (!(p[10] & 0x80)) c |= CAPT_C_NO_PAPER;                 /* "Paper" = port+0x54e6: bit (0x80>>slot) = paper in slot (CheckPaperError FUN_080c156c; idle msg 0x387) */
	if (p[13] & 0x40) c |= CAPT_C_DOOR_OPEN;                   /* port+0x54e9 & 0x40 -> SetBit 9 (0x200) -> id 0x12f (class 3) -> UI code 0x2e s_door_open */
	if (p[13] & 0x01) c |= CAPT_C_JAM;                         /* port+0x54e9 & 0x01 -> SetBit 8 (0x100) -> id 0x12d (class 2) -> UI code 0x3b/0x3c s_jam2 */
	if ((p[12] & 0x02) || (p[9] & 0x80)) c |= CAPT_C_SERVICE;  /* port+0x54e8 & 2, port+0x54e5 < 0 -> SetBit 7 (0x80) -> id 0xc9 (class 8) -> UI code 0x8004 s_service_call */
	return c;
}

/* A0A4 entry decode = C_Port::CheckCartridgeStatus (FUN_080c79a0) + per-entry FUN_080c5508 (flag chain 0x80,0x20,0x01,0x08,flag2&8,flag2&0x40,0x10,0x02) */
unsigned capt_cartridge_conditions(const struct capt_cartridge *e, int n)
{
	unsigned c = 0; int i;
	for (i = 0; i < n; i++) {
		unsigned f = e[i].flag, f2 = e[i].flag2;
		if (e[i].id > 3) continue;                           /* ids 0..3 = toner slots (mask bit 1<<id); 0x10..0x13 = drums etc. are not decoded */
		if (f & 0x80) c |= CAPT_C_NO_CARTRIDGE;              /* 7250 -> error bit 10 (0x400) -> id 0x192 -> UI code 0x3e s_no_cartridge */
		if (f & 0x40) c |= CAPT_C_TONER_MEM;                 /* 7240 -> ready-with-warning code 5 s_warn_toner_mem */
		if (!(f & (0x80 | 0x20 | 0x01 | 0x08 | 0x10)) && !(f2 & (0x08 | 0x40)) && (f & 0x02)) c |= CAPT_C_TONER_LOW;   /* 7234 -> code 2 s_warn_toner1 */
	}
	return c;
}

enum capt_state capt_status_summary(const struct capt_status *s)
{
	/* STATUS1 (payload+8) = 0001 idle, 0081 receiving, 0085 printing, 4085/4001 ejecting (capture-002..007, 1911 samples).
	 * payload+10 bit7 is the slot-0 "paper present" bit (Canon "Paper" field), NOT an error bit: the old "bit7 clear = error" reading was the
	 * same bit with the wrong meaning.  S0 bit1 (sleep) and bit4 (offline) are normal idle states and are not errors. */
	unsigned c = capt_status_conditions(s);
	if (c & (CAPT_C_DOOR_OPEN | CAPT_C_JAM | CAPT_C_SERVICE)) return CAPT_ST_ERROR;
	if (c & CAPT_C_NO_PAPER) return CAPT_ST_NO_PAPER;
	if (s->st[1] & 0x40C4) return (s->st[1] & 0x0080) && !(s->st[1] & 0x0004) ? CAPT_ST_BUSY : CAPT_ST_PRINTING;
	return CAPT_ST_READY;
}
const char *capt_state_name(enum capt_state s)
{
	static const char *n[] = { "ready", "printing", "busy", "no paper", "error", "not initialised", "powering up" };
	return (unsigned)s < sizeof n / sizeof *n ? n[s] : "?";
}

static int parse_cartridges(const uint8_t *r, int n, struct capt_cartridge *out, int max)
{
	int cnt = n >= 8 ? (n - 8) / 12 : 0, i;
	if (cnt > max) cnt = max;
	for (i = 0; i < cnt; i++) {
		const uint8_t *e = r + 8 + 12 * i;
		memcpy(out[i].raw, e, 12);
		out[i].flag = e[0]; out[i].id = e[1]; out[i].flag2 = e[10];
	}
	return cnt;
}
int capt_get_cartridges(struct capt_io *io, struct capt_cartridge *out, int max)
{
	uint8_t r[8 + 12 * 16];
	int n = capt_xact(io, PC_GET_CARTRIDGE, NULL, 0, r, sizeof r, 2000);
	if (n < 0) return n;
	return parse_cartridges(r, n, out, max);
}

/* ------------------------------------------------------------ page description */
void capt_page_opts_default(struct capt_page_opts *o)
{
	memset(o, 0, sizeof *o);
	o->paper = CAPT_A4; o->density = 4; o->smooth = 1;
}

static const struct { unsigned code_d0, code_pi; struct capt_geom g; } paper_tbl[CAPT_PAPER_COUNT] = {
	[CAPT_A4]        = { 0x02, 9,  { 4960, 7014, 592, 6778, 120, 7 } },
	[CAPT_LETTER]    = { 0x0d, 1,  { 5100, 6600, 608, 6364, 118, 4 } },
	[CAPT_LEGAL]     = { 0x0c, 5,  { 5100, 8400, 608, 8164, 118, 4 } },   /* all geometry measured via oracle (docs/06) */
	[CAPT_A5]        = { 0x03, 0xb,{ 3496, 4960, 408, 4724, 118, 8 } },
	[CAPT_B5]        = { 0x07, 0xd,{ 4298, 6070, 508, 5834, 118, 5 } },
	[CAPT_EXECUTIVE] = { 0x0a, 7,  { 4350, 6300, 516, 6064, 118, 2 } },
};
int capt_paper_geom(enum capt_paper p, struct capt_geom *g)
{
	if ((unsigned)p >= CAPT_PAPER_COUNT) return CAPT_EARG;
	*g = paper_tbl[p].g; return 0;
}

/* baseline D0A0 payload (68 bytes) as sent on the wire for A4/plain/density 4 (docs/06) */
static const uint8_t d0a0_base[68] = {
	0x01,0x00,0xe3,0x09,0x02,0x00,0x00,0x00, 0x0c,0x1c,0x1c,0x1c, 0x01,0x11,0x04,0x00,
	0x01,0x01,0x02,0x00,0x00,0x00,0x76,0x00, 0x76,0x00,0x50,0x02,0x7a,0x1a,0x60,0x13,
	0x66,0x1b,0x00,0x00,0x01,0x00,0x00,0x00, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0
};

size_t capt_build_d0a0(uint8_t *out, const struct capt_page_opts *o)
{
	uint8_t p[68]; struct capt_geom g;
	memcpy(p, d0a0_base, sizeof p);
	if (capt_paper_geom(o->paper, &g)) return 0;
	p[4] = (uint8_t)paper_tbl[o->paper].code_d0;
	p[8] = (uint8_t)(((o->density ? o->density : 4) - 1) * 4);
	if (!o->smooth) p[18] = 0;
	p[0] = o->halftone_off ? 0 : o->toner_save ? 2 : 1;     /* wire D0A0[0]: 0 solid, 1 normal, 2 draft (mock-printer capture) */
	if (o->media) {                                           /* D0A0[36] media code, D0A0[12] media class (mock capture: plain 1, thick 4, labels 0x28; others unverified) */
		p[36] = (uint8_t)o->media;
		p[12] = o->media == CAPT_THICK ? 4 : o->media == CAPT_LABELS ? 0x28 : 1;
	}
	/* NOTE duplex: Canon's monitor sets D0A0[0]=0 and D0A0[5]=4 (front) / 2 (back) for 2-page duplex jobs; multi-page flow was observed only
	   against the mock (not faithful) -> duplex/multipage framing below is a best guess and must be verified on hardware. */
	if (o->duplex != CAPT_SIMPLEX) { p[0] = 0; p[5] = o->back_side ? 2 : 4; }
	capt_put16(p + 26, g.print_bytes_w); capt_put16(p + 28, g.print_lines);
	capt_put16(p + 30, g.px_w);          capt_put16(p + 32, g.px_h);
	return capt_pkt(out, 72, IC_PAGE_PARAMS, p, sizeof p);
}

size_t capt_build_compound(uint8_t *out, size_t cap, const struct capt_page_opts *o)
{
	uint8_t in[96 - 4], a4[8] = { 1, 4, 1, 1, 0, 0xf0, 0, 0 };
	size_t n = 0;
	if (o->halftone_off) { a4[0] = 2; a4[1] = 0x0c; }
	if (o->toner_save) { a4[0] = 3; a4[1] = 0x0c; a4[5] = 0xf1; }
	if (!capt_build_d0a0(in, o)) return 0;
	n = 72;
	n += capt_pkt(in + n, sizeof in - n, IC_COMP_PARAMS, a4, 8);
	n += capt_pkt(in + n, sizeof in - n, IC_D0A1, NULL, 0);
	n += capt_pkt(in + n, sizeof in - n, IC_D0A2, NULL, 0);
	return capt_pkt(out, cap, IC_COMPOUND_DATA, in, n);
}

size_t capt_build_job_info(uint8_t *out, size_t cap, unsigned phase, unsigned state, const uint8_t when[8], const char *host, const char *user, const char *title, unsigned job_id, int tz_std, int tz_dst)
{
	uint8_t p[72 + 30 + 40 + 256]; size_t i, n1 = 0, n2 = 0, n3 = 0, o = 72;
	const char *s1 = host ? host : "", *s2 = user ? user : "", *s3 = title ? title : "";
	memset(p, 0, sizeof p);
	while (s1[n1] && n1 < 15) n1++;          /* Canon: str1 max 0x1e bytes, str2 0x28, str3 0x100 (UTF-16LE, ASCII here) */
	while (s2[n2] && n2 < 20) n2++;
	while (s3[n3] && n3 < 128) n3++;
	p[4] = (uint8_t)phase;
	capt_put16(p + 8, (unsigned)(2 * n1)); capt_put16(p + 10, (unsigned)(2 * n2)); capt_put16(p + 12, (unsigned)(2 * n3));
	p[16] = (uint8_t)state; p[17] = 1;       /* p[16] = state (1 start, 2 page sent, 6 done); p[17] = byte from the job struct (always 1 in captures) */
	capt_put16(p + 18, job_id);              /* HW-verified: = CAPT job id from the A2A0 reply; wrong => 0x90 EC_JOBID_MISMATCH */
	capt_put16(p + 20, (unsigned)tz_std & 0xffff);   /* param_4 = Bias+StandardBias (minutes west of UTC), FUN_080bf90c */
	capt_put16(p + 22, (unsigned)tz_dst & 0xffff);   /* param_5 = Bias+DaylightBias */
	memcpy(p + 24, when, 8);
	for (i = 0; i < n1; i++, o += 2) p[o] = (uint8_t)s1[i];
	for (i = 0; i < n2; i++, o += 2) p[o] = (uint8_t)s2[i];
	for (i = 0; i < n3; i++, o += 2) p[o] = (uint8_t)s3[i];
	(void)i;
	return capt_pkt(out, cap, PCR_SET_JOB_INFO, p, o);
}

/* ------------------------------------------------------------ job engine */
#define JOBINFO(j, out, cap, phase, state) capt_build_job_info(out, cap, phase, state, (j)->when, (j)->host, (j)->user, (j)->title, (j)->reserve_id, (j)->tz_std, (j)->tz_dst)
static int cmd_ok(struct capt_io *io, unsigned cmd, const uint8_t *pl, size_t n)
{
	uint8_t r[16];
	/* E0A7 is queued behind the page data in the pipe/backend, so the printer answers only after it has taken the whole page */
	int e = capt_xact(io, cmd, pl, n, r, sizeof r, (cmd == PCR_START_PRINT ? 60000 : 5000));
	if (e < 0) return e;
	return (e >= 6 && capt_get16(r + 4) != 0) ? CAPT_ESTATUS : 0;
}
#define XA(io, cmd, pl, n) do { int e_ = cmd_ok(io, cmd, pl, n); if (e_) return e_; } while (0)

/* poll A0A8 every 100 ms until (S0 low byte & mask) == 0; returns 0 ok, -1 timeout (errors from the status read are ignored, as in Canon's loops) */
static int wait_s0_clear(struct capt_io *io, unsigned mask, unsigned timeout_ms)
{
	unsigned waited = 0; struct capt_status s;
	for (;;) {
		if (capt_get_status(io, &s) == 0 && !(s.st[0] & mask)) return 0;
		if (waited >= timeout_ms) return -1;
		if (io->sleep_ms) io->sleep_ms(io->ctx, 100);
		waited += 100;
	}
}

static int job_begin_steps(struct capt_job *j, struct capt_io *io, const uint8_t when[8], const char *host, const char *user, const char *title)
{
	static const uint8_t res[8] = { 0, 0, 0x1e, 0, 0, 0, 0, 0 }, led[16] = { 0x13 }, pd[2] = { 1, 0 };
	static const uint8_t goon[16] = { 0xee, 0xdb, 0xea, 0xad }, zp[4] = { 0 };
	uint8_t pk[512], r[16];
	int e;
	{ int dup = j->duplex, tzs = j->tz_std, tzd = j->tz_dst; unsigned tp = j->total_pages; memset(j, 0, sizeof *j); j->duplex = dup; j->tz_std = tzs; j->tz_dst = tzd; j->total_pages = tp; }
	j->io = io; memcpy(j->when, when, 8);
	/* HW (Canon job1 vs job2 trace, capture-003): the init steps E0BA/E0A3/E0A2/E0A4/E0A5 are sent only on the first job after power-up
	 * (S0 bit5 = 0x20 set); later jobs skip them.  E0A5 resets the page counters, so the E0A7 page serial restarts at 1 after it; otherwise
	 * it is the highest printer page counter + 1.  Repeating E0A5 on later jobs desynchronised the serial and stalled the printer. */
	{ struct capt_status s0; unsigned m = 0;
	  j->need_init = 1;
	  if (capt_get_status(io, &s0) == 0) {
		j->need_init = (s0.st[0] & 0x10) != 0;   /* Canon PrintContinuous (captmoncnabd FUN_080c1d00): S0 low byte bit4 = OFFLINE (GetStatus logs "detect OffLine"); offline -> ClearMisprint, ClearError, DiscardData, DoOnline(GoOnline E0A5); online -> none (docs/09) */
		m = s0.page_decoding; if (s0.page_printing > m) m = s0.page_printing; if (s0.page_out > m) m = s0.page_out; if (s0.page_completed > m) m = s0.page_completed; }
	  j->page_serial = j->need_init ? 1 : m + 1;
	  j->first_serial = j->page_serial; j->start_next = j->page_serial; }
	strncpy(j->host, host ? host : "", sizeof j->host - 1);
	strncpy(j->user, user ? user : "", sizeof j->user - 1);
	strncpy(j->title, title ? title : "", sizeof j->title - 1);
	/* HARDWARE (Nov 14): A2A0 is answered with status 0x0087 ("already reserved"/busy) on about every second attempt; the failed attempt
	 * appears to clear the state, so the next one succeeds.  Canon sends A0A4 + A3AF just before each reserve.  Retry up to 3 times. */
	{
		int tries;
		for (tries = 0; tries < 3; tries++) {
			uint8_t q[24], cq[8 + 12 * 4]; int cn;
			if (CANCELLED()) return CAPT_ECANCEL;
			cn = capt_xact(io, PC_GET_CARTRIDGE, NULL, 0, cq, sizeof cq, 3000);
			j->ncart = cn >= 8 ? parse_cartridges(cq, cn, j->cart, 4) : 0;
			(void)capt_xact(io, PC_GET_TIME, NULL, 0, q, sizeof q, 3000);
			e = capt_xact(io, PC_RESERVE_UNIT, res, 8, r, sizeof r, 3000);
			if (e < 8) return e < 0 ? e : CAPT_EPROTO;
			if (capt_get16(r + 4) == 0) break;
			io->sleep_ms(io->ctx, 500);
		}
		if (tries == 3) return CAPT_ESTATUS;
	}
	j->reserve_id = capt_get16(r + 6);
	j->reserved = 1;                         /* from here on every failure must release (capt_job_begin wrapper) */
	if (CANCELLED()) return CAPT_ECANCEL;
	/* Canon C_Port::ReservePrinter (FUN_080a7ddc): after A2A0, poll status up to 2 s until S0 low byte bits 0x41 are clear, then continue regardless */
	wait_s0_clear(io, 0x41, 2000);
	{	/* Canon FUN_080a6930 + EngineSetTime (FUN_080af404): set the printer clock iff status payload+8 bit0 is clear (clock never set, e.g. after
		 * power-up: capture-007 first poll p+8=0 -> A3A9, all later polls p+8=1 -> none) or the printer's GetTime reply byte 15 (JOBTIME byte 7, the
		 * daylight flag) differs from ours.  Packet = 4B header + JOBTIME(8) + zeros (162 bytes; the other non-zero bytes in Canon's capture are
		 * uninitialised heap padding).  Result is ignored, as in Canon. */
		struct capt_status cs; uint8_t gt[24], st_[162 - 4], rr[16];
		int need = 0;
		if (capt_get_status(io, &cs) == 0) {
			if (!(cs.st[1] & 1)) need = 1;
			else if (capt_xact(io, PC_GET_TIME, NULL, 0, gt, sizeof gt, 3000) >= 16 && gt[15] != when[7]) need = 1;
		}
		if (need) { memset(st_, 0, sizeof st_); memcpy(st_, when, 8); (void)capt_xact(io, PC_SET_TIME, st_, sizeof st_, rr, sizeof rr, 3000); }
	}
	XA(io, PCR_SET_LED, led, 16);
	(void)capt_xact(io, PC_GET_TIME, NULL, 0, r, sizeof r, 3000);          /* A3AF GetTime, as in capture-007 (query only) */
	{	/* Canon C_Port::LeaveSleep (FUN_080a8114): only when the engine sleeps (S0 bit1 / status payload+12 bit15): E0BA(1,0) = SetPowerDown(wake),
		 * then poll status until payload+12 bit15 clears (<= 15 s; capture-007: ~160 ms).  Job 79 (S0=0x8c31) had no E0BA, job 80 (0x8c13) had it. */
		struct capt_status ss;
		if (capt_get_status(io, &ss) == 0 && (ss.st[0] & 0x02)) {
			unsigned waited = 0;
			XA(io, PCR_SET_POWER_DOWN, pd, 2);
			for (;;) {
				if (capt_get_status(io, &ss) == 0 && !(ss.st[3] & 0x8000)) break;
				if (waited >= 15000) return CAPT_ETIMEOUT;
				if (io->sleep_ms) io->sleep_ms(io->ctx, 50);
				waited += 50;
			}
		}
	}
	{ size_t n = JOBINFO(j, pk, sizeof pk, 0, 1);
	  XA(io, PCR_SET_JOB_INFO, pk + 4, n - 4); }
	j->info_sent = 1;
	if (CANCELLED()) return CAPT_ECANCEL;
	if (j->need_init) {
		XA(io, PCR_CLEAR_MISPRINT, NULL, 0);
		XA(io, PCR_UNKNOWN_E0A2, NULL, 0);
		XA(io, PCR_DISCARD_DATA, NULL, 0);
		/* Canon DoOnline (FUN_080c03c8): wait up to 2 s for S0 bit1 (0x02) to clear; if it never does, GoOnline is NOT sent (error 0x15) */
		if (wait_s0_clear(io, 0x02, 2000)) return CAPT_ESTATUS;
		XA(io, PCR_GO_ONLINE, goon, 16);
	}
	XA(io, PCR_SET_PRINTED_PAGES, zp, 4);
	return 0;
}

int capt_job_begin(struct capt_job *j, struct capt_io *io, const uint8_t when[8], const char *host, const char *user, const char *title)
{
	int e = job_begin_steps(j, io, when, host, user, title);
	if (e) {
		if (CANCELLED()) e = CAPT_ECANCEL;
		capt_job_abort(j, e);            /* no-op unless A2A0 succeeded: Canon releases whenever it holds the reservation */
	}
	return e;
}

/* write a frame, or append it to the hold buffer while a duplex front page is being held */
static int jw(struct capt_job *j, const uint8_t *b, size_t n)
{
	if (!j->holding) return j->io->write(j->io->ctx, b, n) < 0 ? CAPT_EIO : 0;
	if (j->hold_n + n > j->hold_cap) {
		size_t nc = j->hold_cap ? j->hold_cap * 2 : 1 << 20; uint8_t *p;
		while (nc < j->hold_n + n) nc *= 2;
		if (!(p = (uint8_t *)realloc(j->hold, nc))) return CAPT_EIO;
		j->hold = p; j->hold_cap = nc;
	}
	memcpy(j->hold + j->hold_n, b, n); j->hold_n += n;
	return 0;
}

static int gate_block(struct capt_job *j, unsigned timeout_ms, unsigned serial);
/* release the held front page; lone = no back page follows -> Canon QueuePage type 2 (D0A0 payload[5] bits 1,2 cleared; FUN_080a0e84) */
static int release_hold(struct capt_job *j, int lone)
{
	int e = 0;
	if (!j->hold_n) return 0;
	{ int ge = gate_block(j, 120000, j->front_serial); if (ge) { j->hold_n = 0; return ge; } }
	if (lone) j->hold[13] = (uint8_t)(j->hold[13] & ~6);   /* first frame is the D0A9: a9 d0 sz sz | a0 d0 48 00 | payload; payload[5] at 13 */
	if (j->io->write(j->io->ctx, j->hold, j->hold_n) < 0) e = CAPT_EIO;
	j->hold_n = 0;
	return e;
}

/* Canon C_Send::SendDataQueue (FUN_080a1008, Windows twin FUN_1001f590; docs/07 sec 8): a D0A9 block is started only after a status poll showing
 * (a) S0 bit 0x04 (printer busy receiving/decoding a page) clear, and (b) the page-ahead window: pages completed ("page safe", A0A8 payload +20)
 * + max(2, W) >= the block's page serial, W = byte 0x36 of the A1A1 reply minus a per-model term (4 on this printer; Canon job 80 sent page 3 at
 * completed=0, so W >= 3).  We use the proven lower bound CAPT_PAGE_WINDOW = 3 (never further ahead than Canon).  serial 0 = no window check. */
static int gate_block(struct capt_job *j, unsigned timeout_ms, unsigned serial)
{
	unsigned waited = 0; struct capt_status s;
	for (;;) {
		int e;
		if (CANCELLED()) return CAPT_ECANCEL;   /* safe point: no page is in flight yet (D0A9 not sent), earlier pages already got their E0A7 */
		e = capt_get_status(j->io, &s);
		if (e) return e;
		if (j->cb) j->cb(j->cbctx, &s);
		if (!(s.st[0] & 0x0004) && (!serial || s.page_completed + CAPT_PAGE_WINDOW >= serial)) return 0;
		if (waited >= timeout_ms) return CAPT_ETIMEOUT;
		if (j->io->sleep_ms) j->io->sleep_ms(j->io->ctx, 50);
		waited += 50;
	}
}

int capt_page_begin(struct capt_job *j, const struct capt_page_opts *o)
{
	uint8_t cmp[96];
	size_t n;
	struct capt_page_opts o2 = *o;
	j->opts = *o;
	if (o->duplex != CAPT_SIMPLEX && o->back_side) {
		/* Canon duplex pair (docs/10, proven on Canon's wire captures): D0A9(type 4: D0A0[5]=4) is followed by the BACK page's data
		 * (rotated 180 by the caller), then the held FRONT page's data, then D0A9(type 3: [5]=2) - see capt_page_end. */
		o2.back_side = 0;
		n = capt_build_compound(cmp, sizeof cmp, &o2);
		if (!n) return CAPT_EARG;
		{ int e = gate_block(j, 120000, j->page_serial); if (e) return e; }
		return j->io->write(j->io->ctx, cmp, n) < 0 ? CAPT_EIO : 0;
	}
	n = capt_build_compound(cmp, sizeof cmp, o);
	if (!n) return CAPT_EARG;
	j->holding = o->duplex != CAPT_SIMPLEX;   /* front page: hold (D0A9 + data) until a back page begins or the job ends */
	if (!j->holding) { int e = gate_block(j, 120000, j->page_serial); if (e) return e; }
	return jw(j, cmp, n);
}

int capt_page_data(struct capt_job *j, const uint8_t *d, size_t n)
{
	static uint8_t pk[CAPT_MAX_PKT];            /* NOTE: not reentrant */
	while (n) {
		size_t k = n > 0xff00 ? 0xff00 : n, len = capt_pkt(pk, sizeof pk, IC_VIDEO_DATA, d, k);
		if (!len || jw(j, pk, len)) return CAPT_EIO;
		d += k; n -= k;
	}
	return 0;
}

/* Canon C_Send::SendStartPrint gate (FUN_080a0bfc + GetStatus, verified on capture-007 status polls): E0A7(n) is sent only after the printer's
 * "pages decoded" counter (status payload +14) has reached n, strictly in order.  E1A1(state 2) follows the FIRST E0A7 of the job. */
static int pump_start_print(struct capt_job *j, unsigned timeout_ms)
{
	unsigned waited = 0; struct capt_status s; uint8_t sp[2] = { 0, 0 }, jp[512];
	while (j->start_next <= j->sent_last) {
		int e = capt_get_status(j->io, &s);
		if (e) return e;
		if (j->cb) j->cb(j->cbctx, &s);
		if (s.page_decoding >= j->start_next) {
			sp[0] = (uint8_t)j->start_next;
			XA(j->io, PCR_START_PRINT, sp, 2);
			j->start_next++;
			if (!j->started_any) {
				size_t n = JOBINFO(j, jp, sizeof jp, j->total_pages, 2);   /* p[4] = job total pages if known, else 0 (docs/09) */
				XA(j->io, PCR_SET_JOB_INFO, jp + 4, n - 4);
				j->started_any = 1;
			}
			continue;
		}
		if (waited >= timeout_ms) return CAPT_ETIMEOUT;
		if (j->io->sleep_ms) j->io->sleep_ms(j->io->ctx, 100);
		waited += 100;
	}
	return 0;
}

int capt_page_end(struct capt_job *j)
{
	uint8_t pk[8];
	size_t n = capt_pkt(pk, sizeof pk, IC_BLACK_END, NULL, 0);
	if (jw(j, pk, n)) return CAPT_EIO;
	if (j->opts.duplex != CAPT_SIMPLEX) {
		/* Canon wire (docs/10): D0A9[5]=4, back page data (rotated), C0A4, front page data, C0A4, D0A9[5]=2 | then E0A7 per page as the printer decodes them */
		uint8_t cmp[96];
		if (!j->opts.back_side) { j->holding = 0; j->front_pending = 1; j->front_serial = j->page_serial++; j->pages_sent++; return 0; }
		/* back page data is out: now the held front page's data (without its D0A9: the pair's D0A9 [5]=4 went out in page_begin) */
		if (j->hold_n) {
			size_t hl = capt_get16(j->hold + 2);
			if (hl >= j->hold_n || j->io->write(j->io->ctx, j->hold + hl, j->hold_n - hl) < 0) return CAPT_EIO;
			j->hold_n = 0;
		}
		n = capt_build_compound(cmp, sizeof cmp, &j->opts);
		if (!n) return CAPT_EARG;
		{ int e = gate_block(j, 120000, 0); if (e) return e; }
		if (j->io->write(j->io->ctx, cmp, n) < 0) return CAPT_EIO;
		j->page_serial++; j->pages_sent++; j->front_pending = 0;
		j->sent_last = j->page_serial - 1;
		return pump_start_print(j, 120000);
	}
	j->page_serial++; j->pages_sent++;
	j->sent_last = j->page_serial - 1;
	return pump_start_print(j, 120000);
}

int capt_job_page(struct capt_job *j, const struct capt_page_opts *o,
                  const uint8_t *const *bands, const size_t *len, int nb)
{
	int i, e = capt_page_begin(j, o);
	for (i = 0; !e && i < nb; i++) e = capt_page_data(j, bands[i], len[i]);
	return e ? e : capt_page_end(j);
}

int capt_compress_page(const struct capt_page_opts *o, capt_line_fn fetch, void *ctx, const struct capt_page_sink *sink)
{
	enum { PRE = 16 };
	struct capt_geom g;
	static const struct hiscoa_params hp = { 1, 4, 0, -16, 0 };
	size_t ls, cap, stage_n = 0;
	uint8_t *buf, *out, *stage;
	unsigned y = 0;
	int e;
	if (capt_paper_geom(o->paper, &g)) return CAPT_EARG;
	ls = g.print_bytes_w;
	cap = ls * CAPT_BAND_LINES * 2 + 1024;
	buf = (uint8_t *)calloc(1, ls * (CAPT_BAND_LINES + PRE));
	out = (uint8_t *)malloc(cap);
	stage = (uint8_t *)malloc(0xff00 + cap);
	if (!buf || !out || !stage) { free(buf); free(out); free(stage); return CAPT_EIO; }
	e = sink->begin ? sink->begin(sink->ctx) : 0;
	while (!e && y < g.print_lines) {
		unsigned nl = g.print_lines - y < CAPT_BAND_LINES ? g.print_lines - y : CAPT_BAND_LINES, i;
		size_t k;
		for (i = 0; i < nl && !e; i++) e = fetch(ctx, buf + ls * (PRE + i), y + i) ? CAPT_EDATA : 0;
		if (e) break;
		k = hiscoa_compress_band(out, cap, buf + ls * PRE, (unsigned)ls, nl, HISCOA_EOB_NORMAL, &hp);
		if (!k) { e = CAPT_EPROTO; break; }
		if (getenv("CAPT_DEBUG")) fprintf(stderr, "DEBUG: band y=%u nl=%u bytes=%zu\n", y, nl, k);
		memcpy(stage + stage_n, out, k); stage_n += k;
		while (stage_n >= 0xff00) {
			size_t take = 0xff00;
			e = sink->data(sink->ctx, stage, take);
			if (e) break;          /* stop at the first failure: the old loop kept going and a later success could overwrite this error */
			memmove(stage, stage + take, stage_n - take); stage_n -= take;
		}
		if (e) break;
		y += nl;
	}
	if (!e) {
		/* Canon ends every page's band stream with a 4-byte terminator = an EMPTY band closed with eob type LAST (FE end code, eob=01, ones) = bd 3c bc bc;
		 * present in all 40 filter streams and both duplex wire captures (docs/10).  Without it the printer's decoder is not reset for the next page block. */
		size_t k = hiscoa_compress_band(out, cap, buf + ls * PRE, (unsigned)ls, 0, HISCOA_EOB_LAST, &hp);
		if (k != 4) e = CAPT_EPROTO;
		else { memcpy(stage + stage_n, out, k); stage_n += k; }
	}
	if (!e && stage_n) e = sink->data(sink->ctx, stage, stage_n);
	if (!e && sink->end) e = sink->end(sink->ctx);
	free(buf); free(out); free(stage);
	return e;
}

/* sink that feeds the compressed page stream into this job's engine (Linux: the filter runs the CAPT conversation itself) */
struct job_sink { struct capt_job *j; const struct capt_page_opts *o; };
static int js_begin(void *c) { struct job_sink *s = (struct job_sink *)c; return capt_page_begin(s->j, s->o); }
static int js_data(void *c, const uint8_t *d, size_t n) { return capt_page_data(((struct job_sink *)c)->j, d, n); }
static int js_end(void *c) { return capt_page_end(((struct job_sink *)c)->j); }

int capt_job_page_lines(struct capt_job *j, const struct capt_page_opts *o, capt_line_fn fetch, void *ctx)
{
	struct job_sink js = { j, o };
	struct capt_page_sink k = { js_begin, js_data, js_end, &js };
	return capt_compress_page(o, fetch, ctx, &k);
}

int capt_job_flush(struct capt_job *j)
{
	if (!j->front_pending) return 0;
	/* Canon capture-007 (3 pages): lone front page = D0A9(front) C0A0.. C0A4, then its E0A7 once decoded */
	j->front_pending = 0;
	{ int e = release_hold(j, 1); if (e) return e; }
	j->sent_last = j->front_serial;
	return pump_start_print(j, 120000);
}

static int release_printer(struct capt_job *j)
{
	uint8_t id[2]; capt_put16(id, j->reserve_id);
	j->reserved = 0;                     /* cleared whether or not the write works, as Canon's FUN_080a67f8 does */
	free(j->hold); j->hold = NULL; j->hold_cap = j->hold_n = 0;
	return cmd_ok(j->io, PCR_RELEASE_UNIT, id, 2);
}

int capt_job_finish(struct capt_job *j, unsigned state)
{
	uint8_t jp[512];
	if (!j->reserved) return 0;
	if (j->started_any) {   /* Canon: E1A1(final state) after the last E0A7, then E0A9 while the pages are still printing */
		size_t n = JOBINFO(j, jp, sizeof jp, j->total_pages ? j->total_pages : j->pages_sent, state);   /* all pages sent -> total known */
		int e = cmd_ok(j->io, PCR_SET_JOB_INFO, jp + 4, n - 4);
		if (e) { release_printer(j); return e; }
	}
	return release_printer(j);
}

int capt_job_end(struct capt_job *j) { return capt_job_finish(j, 6); }

int capt_job_abort(struct capt_job *j, int cause)
{
	uint8_t jp[512];
	int link_ok = !(cause == CAPT_EIO || cause == CAPT_ETIMEOUT || cause == CAPT_EPROTO);   /* Canon FUN_080c5278: no final E1A1 on a failed link */
	if (!j->reserved) return 0;
	if (link_ok && j->info_sent) {
		size_t n = JOBINFO(j, jp, sizeof jp, j->total_pages ? j->total_pages : j->pages_sent, cause == CAPT_ECANCEL ? 4 : 5);
		(void)cmd_ok(j->io, PCR_SET_JOB_INFO, jp + 4, n - 4);
	}
	return release_printer(j);
}

int capt_job_wait_page(struct capt_job *j, unsigned pageno, unsigned timeout_ms,
                       void (*cb)(void *, const struct capt_status *), void *cbctx)
{
	unsigned waited = 0;
	struct capt_status s;
	while (waited <= timeout_ms) {
		int e = capt_get_status(j->io, &s);
		if (e) return e;
		if (cb) cb(cbctx, &s);
		{ unsigned want = j->first_serial + pageno - 1; if (s.page_out >= want || s.page_completed >= want) return 0; }
		if (j->io->sleep_ms) j->io->sleep_ms(j->io->ctx, 100);
		waited += 100;
	}
	return CAPT_ETIMEOUT;
}

int capt_wait_ready_for_data(struct capt_io *io, unsigned timeout_ms)
{
	unsigned waited = 0;
	struct capt_status s;
	while (waited <= timeout_ms) {
		int e = capt_get_status(io, &s);
		if (e) return e;
		if (!(s.st[0] & 0x0004)) return 0;          /* STATUS0 bit2: buffer full */
		if (io->sleep_ms) io->sleep_ms(io->ctx, 50);
		waited += 50;
	}
	return CAPT_ETIMEOUT;
}
