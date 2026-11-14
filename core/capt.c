#include "capt.h"
#include <stdlib.h>
#include <string.h>
#include "../hiscoa/hiscoa-compress.h"

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
	return 0;
}

enum capt_state capt_status_summary(const struct capt_status *s)
{
	/* Derived from the real capture (evidence/capture-002/a0a8-distinct.txt): STATUS2 bit7 is "not error" and stays set;
	 * STATUS1 = 0001 idle, 0081 receiving, 0085 printing, 4085/4001 ejecting; STATUS0 bits5-4 clear after E0A5.
	 * Paper-out bit not identified: STATUS0 bit1 was DISPROVED (S0=8c13 both with tray empty and loaded). */
	if (!(s->st[2] & 0x0080)) return CAPT_ST_ERROR;
	/* STATUS0 bits 5-4 stay set while the printer is idle between jobs (8c10/8c13 observed), so they are NOT an error/busy indication */
	if (s->st[1] & 0x40C4) return (s->st[1] & 0x0080) && !(s->st[1] & 0x0004) ? CAPT_ST_BUSY : CAPT_ST_PRINTING;
	return CAPT_ST_READY;
}
const char *capt_state_name(enum capt_state s)
{
	static const char *n[] = { "ready", "printing", "busy", "no paper", "error", "not initialised", "powering up" };
	return (unsigned)s < sizeof n / sizeof *n ? n[s] : "?";
}

int capt_get_cartridges(struct capt_io *io, struct capt_cartridge *out, int max)
{
	uint8_t r[8 + 12 * 16];
	int n = capt_xact(io, PC_GET_CARTRIDGE, NULL, 0, r, sizeof r, 2000);
	if (n < 0) return n;
	int cnt = (n - 8) / 12, i;
	if (cnt > max) cnt = max;
	for (i = 0; i < cnt; i++) {
		const uint8_t *e = r + 8 + 12 * i;
		memcpy(out[i].raw, e, 12);
		out[i].flag = e[0]; out[i].id = e[1]; out[i].flag2 = e[10];
	}
	return cnt;
}

/* ------------------------------------------------------------ page description */
void capt_page_opts_default(struct capt_page_opts *o)
{
	memset(o, 0, sizeof *o);
	o->paper = CAPT_A4; o->density = 4; o->smooth = 1;
}

static const struct { unsigned code_d0, code_pi; struct capt_geom g; } paper_tbl[CAPT_PAPER_COUNT] = {
	[CAPT_A4]        = { 0x02, 9,  { 4960, 7014, 592, 6778, 120 } },
	[CAPT_LETTER]    = { 0x0d, 1,  { 5100, 6600, 608, 6364, 118 } },
	[CAPT_LEGAL]     = { 0x0c, 5,  { 5100, 8400, 608, 8164, 118 } },   /* all geometry measured via oracle (docs/06) */
	[CAPT_A5]        = { 0x03, 0xb,{ 3496, 4960, 408, 4724, 118 } },
	[CAPT_B5]        = { 0x07, 0xd,{ 4298, 6070, 508, 5834, 118 } },
	[CAPT_EXECUTIVE] = { 0x0a, 7,  { 4350, 6300, 516, 6064, 118 } },
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

size_t capt_build_job_info(uint8_t *out, unsigned phase, unsigned state, const uint8_t when[8], const char *name)
{
	uint8_t p[114]; size_t i;
	memset(p, 0, sizeof p);
	p[4] = (uint8_t)phase; p[8] = 0x0e; p[10] = 6; p[12] = 0x16;
	p[16] = (uint8_t)state; p[17] = 1; p[18] = 2; p[22] = 0x4c; p[23] = 0x30;
	memcpy(p + 24, when, 8);
	for (i = 0; name && name[i] && 72 + 2 * i + 1 < sizeof p; i++) p[72 + 2 * i] = (uint8_t)name[i];
	return capt_pkt(out, 118, PCR_SET_JOB_INFO, p, sizeof p);
}

/* ------------------------------------------------------------ job engine */
static int cmd_ok(struct capt_io *io, unsigned cmd, const uint8_t *pl, size_t n)
{
	uint8_t r[16];
	int e = capt_xact(io, cmd, pl, n, r, sizeof r, 3000);
	if (e < 0) return e;
	return (e >= 6 && capt_get16(r + 4) != 0) ? CAPT_ESTATUS : 0;
}
#define XA(io, cmd, pl, n) do { int e_ = cmd_ok(io, cmd, pl, n); if (e_) return e_; } while (0)

int capt_job_begin(struct capt_job *j, struct capt_io *io, const uint8_t when[8], const char *name)
{
	static const uint8_t res[8] = { 0, 0, 0x1e, 0, 0, 0, 0, 0 }, led[16] = { 0x13 }, pd[2] = { 1, 0 };
	static const uint8_t goon[16] = { 0xee, 0xdb, 0xea, 0xad }, zp[4] = { 0 };
	uint8_t pk[118], r[16];
	int e;
	memset(j, 0, sizeof *j);
	j->io = io; memcpy(j->when, when, 8);
	strncpy(j->name, name ? name : "", sizeof j->name - 1);
	e = capt_xact(io, PC_RESERVE_UNIT, res, 8, r, sizeof r, 3000);
	if (e < 8) return e < 0 ? e : CAPT_EPROTO;
	j->reserve_id = capt_get16(r + 6);
	XA(io, PCR_SET_LED, led, 16);
	XA(io, PCR_SET_POWER_DOWN, pd, 2);
	{ size_t n = capt_build_job_info(pk, 0, 1, when, j->name);
	  XA(io, PCR_SET_JOB_INFO, pk + 4, n - 4); }
	XA(io, PCR_CLEAR_MISPRINT, NULL, 0);
	XA(io, PCR_UNKNOWN_E0A2, NULL, 0);
	XA(io, PCR_DISCARD_DATA, NULL, 0);
	XA(io, PCR_GO_ONLINE, goon, 16);
	XA(io, PCR_SET_PRINTED_PAGES, zp, 4);
	return 0;
}

int capt_page_begin(struct capt_job *j, const struct capt_page_opts *o)
{
	uint8_t cmp[96];
	size_t n = capt_build_compound(cmp, sizeof cmp, o);
	j->opts = *o;
	if (!n) return CAPT_EARG;
	return j->io->write(j->io->ctx, cmp, n) < 0 ? CAPT_EIO : 0;
}

int capt_page_data(struct capt_job *j, const uint8_t *d, size_t n)
{
	static uint8_t pk[CAPT_MAX_PKT];            /* NOTE: not reentrant */
	while (n) {
		size_t k = n > 0xff00 ? 0xff00 : n, len = capt_pkt(pk, sizeof pk, IC_VIDEO_DATA, d, k);
		if (!len || j->io->write(j->io->ctx, pk, len) < 0) return CAPT_EIO;
		d += k; n -= k;
	}
	return 0;
}

int capt_page_end(struct capt_job *j)
{
	uint8_t pk[8], jp[118];
	size_t n = capt_pkt(pk, sizeof pk, IC_BLACK_END, NULL, 0);
	if (j->io->write(j->io->ctx, pk, n) < 0) return CAPT_EIO;
	{ uint8_t sp[2]; sp[0] = j->opts.duplex != CAPT_SIMPLEX ? 4 : 1; sp[1] = 0; XA(j->io, PCR_START_PRINT, sp, 2); }   /* duplex = 4 (mock capture, unconfirmed on HW) */
	n = capt_build_job_info(jp, 1, 2, j->when, j->name);
	XA(j->io, PCR_SET_JOB_INFO, jp + 4, n - 4);
	n = capt_build_job_info(jp, 1, 6, j->when, j->name);
	XA(j->io, PCR_SET_JOB_INFO, jp + 4, n - 4);
	return 0;
}

int capt_job_page(struct capt_job *j, const struct capt_page_opts *o,
                  const uint8_t *const *bands, const size_t *len, int nb)
{
	int i, e = capt_page_begin(j, o);
	for (i = 0; !e && i < nb; i++) e = capt_page_data(j, bands[i], len[i]);
	return e ? e : capt_page_end(j);
}

int capt_job_page_lines(struct capt_job *j, const struct capt_page_opts *o, capt_line_fn fetch, void *ctx)
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
	e = capt_page_begin(j, o);
	while (!e && y < g.print_lines) {
		unsigned nl = g.print_lines - y < CAPT_BAND_LINES ? g.print_lines - y : CAPT_BAND_LINES, i;
		size_t k;
		for (i = 0; i < nl && !e; i++) e = fetch(ctx, buf + ls * (PRE + i), y + i) ? CAPT_EIO : 0;
		if (e) break;
		k = hiscoa_compress_band(out, cap, buf + ls * PRE, (unsigned)ls, nl, HISCOA_EOB_NORMAL, &hp);
		if (!k) { e = CAPT_EPROTO; break; }
		memcpy(stage + stage_n, out, k); stage_n += k;
		while (stage_n >= 0xff00) {
			size_t take = 0xff00;
			e = capt_page_data(j, stage, take);
			memmove(stage, stage + take, stage_n - take); stage_n -= take;
		}
		y += nl;
	}
	if (!e && stage_n) e = capt_page_data(j, stage, stage_n);
	if (!e) e = capt_page_end(j);
	free(buf); free(out); free(stage);
	return e;
}

int capt_job_end(struct capt_job *j)
{
	uint8_t id[2]; capt_put16(id, j->reserve_id);
	XA(j->io, PCR_RELEASE_UNIT, id, 2);
	return 0;
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
		if (s.page_out >= pageno || s.page_completed >= pageno) return 0;
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
