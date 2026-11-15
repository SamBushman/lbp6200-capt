/* LBPC driver: replays the record stream into the CAPT job engine.  Runs inside the class-driver plug-in (and in the host test tool).  GPL-3. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lbpc.h"

enum { P_HEADER, P_JOB_BEGIN, P_BETWEEN, P_IN_PAGE, P_ENDED, P_FAILED };

static unsigned g16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static uint32_t g32(const uint8_t *p) { return g16(p) | (uint32_t)g16(p + 2) << 16; }

static void logf_(struct lbpc_drive *d, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void logf_(struct lbpc_drive *d, const char *fmt, ...)
{
	char line[256]; va_list ap;
	if (!d->log) return;
	va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
	d->log(d->logctx, line);
}

/* CUPS printer-state-reasons from the decoded conditions (same as the Linux filter); cbctx = the drive */
static void report(void *c, const struct capt_status *s)
{
	struct lbpc_drive *d = c; struct capt_job *j = &d->job;
	unsigned cond; int st, key;
	if ((long)time(NULL) - d->rep_lastc >= 5) {   /* toner state can change while a job runs: refresh the cartridge snapshot at most every 5 s, between transactions */
		struct capt_cartridge cr[4]; int n = capt_get_cartridges(j->io, cr, 4);
		d->rep_lastc = (long)time(NULL);
		if (n >= 0) { memcpy(j->cart, cr, sizeof cr); j->ncart = n; }
	}
#ifdef LBP_TRACE   /* diagnostic build only: status changes seen by the gates -> file */
	{ static unsigned last[12]; unsigned cur[12] = { s->st[0], s->st[1], s->st[2], s->st[3], s->st[4], s->st[5], s->st[6], s->page_decoding, s->page_printing, s->page_out, s->page_completed, s->page_received };
	  if (memcmp(last, cur, sizeof cur)) { FILE *f = fopen(LBP_TRACE, "a"); memcpy(last, cur, sizeof cur);
	    if (f) { fprintf(f, "%ld S=%04x %04x %04x %04x %04x %04x %04x dec=%u prt=%u out=%u comp=%u rcv=%u serial=%u\n", (long)time(NULL), cur[0], cur[1], cur[2], cur[3], cur[4], cur[5], cur[6], cur[7], cur[8], cur[9], cur[10], cur[11], j->page_serial); fclose(f); } } }
#endif
	cond = capt_status_conditions(s) | capt_cartridge_conditions(j->cart, j->ncart);
	st = (int)capt_status_summary(s);
	key = (int)cond << 4 | st;
	if (key == d->rep_last) return;
	d->rep_last = key;
	logf_(d, "STATE: %cmedia-empty-error %cmedia-jam-error %cdoor-open-error %cother-error %ctoner-empty-error %ctoner-low-warning %cother-warning",
	      cond & CAPT_C_NO_PAPER ? '+' : '-', cond & CAPT_C_JAM ? '+' : '-', cond & CAPT_C_DOOR_OPEN ? '+' : '-',
	      cond & CAPT_C_SERVICE ? '+' : '-', cond & CAPT_C_NO_CARTRIDGE ? '+' : '-', cond & CAPT_C_TONER_LOW ? '+' : '-',
	      cond & CAPT_C_TONER_MEM ? '+' : '-');
	logf_(d, "INFO: %s", capt_state_name((enum capt_state)st));
}

void lbpc_drive_init(struct lbpc_drive *d, struct capt_io *io, void (*log)(void *, const char *), void *logctx)
{
	memset(d, 0, sizeof *d);
	d->io = io; d->log = log; d->logctx = logctx; d->phase = P_HEADER; d->rep_last = -1;
	capt_cancel_flag = &d->cancel;
}

static int fail(struct lbpc_drive *d, int e)
{
	if (d->phase != P_FAILED && d->started && !d->finished) {
		capt_job_abort(&d->job, d->cancel ? CAPT_ECANCEL : e);   /* Canon: always release once reserved; final E1A1 5/4 only while the link is healthy */
		d->finished = 1;
	}
	d->phase = P_FAILED; d->err = d->cancel ? CAPT_ECANCEL : e;
	return d->err;
}

static int do_job_begin(struct lbpc_drive *d, const uint8_t *p, size_t n)
{
	size_t hl, ul, tl; char host[256], user[256], title[256]; uint8_t when[8];
	if (n < 24) return CAPT_EPROTO;
	hl = g16(p + 18); ul = g16(p + 20); tl = g16(p + 22);
	if (n != 24 + hl + ul + tl || hl > 255 || ul > 255 || tl > 255 || p[4] > 1) return CAPT_EPROTO;
	memcpy(host, p + 24, hl); host[hl] = 0; memcpy(user, p + 24 + hl, ul); user[ul] = 0; memcpy(title, p + 24 + hl + ul, tl); title[tl] = 0;
	memcpy(when, p + 10, 8);
	memset(&d->job, 0, sizeof d->job);
	d->job.total_pages = g32(p); d->job.duplex = p[4];
	d->job.tz_std = (int16_t)g16(p + 6); d->job.tz_dst = (int16_t)g16(p + 8);
	d->started = 1;                       /* capt_job_begin() cleans up after itself when it fails */
	{ int e = capt_job_begin(&d->job, d->io, when, host, user, title);
	  if (e) { d->started = 0; logf_(d, "ERROR: job begin failed (%d)", e); return e; } }
	d->job.cb = report; d->job.cbctx = d;
	return 0;
}

static int do_job_end(struct lbpc_drive *d, unsigned state)
{
	int e = 0;
	if (state != LBPC_END_FAILED && state != LBPC_END_CANCELLED && state != LBPC_END_INCOMPLETE && state != LBPC_END_DONE) return CAPT_EPROTO;
	if (state == LBPC_END_FAILED) { capt_job_abort(&d->job, CAPT_EARG); d->finished = 1; d->phase = P_ENDED; d->err = CAPT_EARG; return 0; }
	/* Canon order (docs/09): lone duplex front page -> its E0A7 when decoded; E1A1(final); E0A9 release; pages keep printing, so lbpc_drive_end() waits for the last one */
	if (d->job.duplex && (d->page % 2)) e = capt_job_flush(&d->job);
	if (e) { logf_(d, "ERROR: starting the print failed (%d)", e); return e; }
	e = capt_job_finish(&d->job, d->cancel ? 4 : state);
	d->finished = 1; d->phase = P_ENDED;
	if (!e && (d->cancel || state == LBPC_END_CANCELLED)) e = CAPT_ECANCEL;
	else if (!e && state == LBPC_END_INCOMPLETE) e = CAPT_EDATA;
	d->err = e; d->end_state = state;
	return 0;                             /* a failing finish is reported through lbpc_drive_end() */
}

static int dispatch(struct lbpc_drive *d, unsigned type, const uint8_t *p, size_t n)
{
	int e;
	switch (type) {
	case LBPC_JOB_BEGIN:
		if (d->phase != P_JOB_BEGIN) return CAPT_EPROTO;
		if ((e = do_job_begin(d, p, n)) != 0) return e;
		d->phase = P_BETWEEN; return 0;
	case LBPC_PAGE_BEGIN:
		if (d->phase != P_BETWEEN || n != 8 || p[0] >= CAPT_PAPER_COUNT || p[1] < 1 || p[1] > 16 || p[4] > 2) return CAPT_EPROTO;
		capt_page_opts_default(&d->po);
		d->po.paper = (enum capt_paper)p[0]; d->po.density = p[1]; d->po.toner_save = p[2] != 0; d->po.smooth = p[3] != 0;
		d->po.duplex = (enum capt_duplex)p[4]; d->po.back_side = p[5] != 0; d->po.halftone_off = p[6] != 0; d->po.media = (enum capt_media)p[7];
		d->page++;
		logf_(d, "INFO: Sending page %u", d->page);
		if ((e = capt_page_begin(&d->job, &d->po)) != 0) { logf_(d, "ERROR: page %u failed (%d)", d->page, e); return e; }
		d->phase = P_IN_PAGE; return 0;
	case LBPC_PAGE_DATA:
		if (d->phase != P_IN_PAGE || n < 1 || n > 0xff00) return CAPT_EPROTO;
		if ((e = capt_page_data(&d->job, p, n)) != 0) { logf_(d, "ERROR: page %u failed (%d)", d->page, e); return e; }
		return 0;
	case LBPC_PAGE_END:
		if (d->phase != P_IN_PAGE || n != 0) return CAPT_EPROTO;
		if ((e = capt_page_end(&d->job)) != 0) { logf_(d, "ERROR: page %u failed (%d)", d->page, e); return e; }
		d->phase = P_BETWEEN; return 0;
	case LBPC_JOB_END:
		if (d->phase != P_BETWEEN || n != 1) return CAPT_EPROTO;
		return do_job_end(d, p[0]);
	}
	return CAPT_EPROTO;
}

int lbpc_drive_feed(struct lbpc_drive *d, const uint8_t *data, size_t n)
{
	while (n) {
		size_t take;
		if (d->phase == P_FAILED) return d->err;
		if (d->phase == P_ENDED) return d->err ? d->err : CAPT_EPROTO;   /* nothing may follow JOB_END */
		if (d->phase == P_HEADER) {
			take = LBPC_HDR_LEN - d->have; if (take > n) take = n;
			memcpy(d->hdr + d->have, data, take); d->have += take; data += take; n -= take;
			if (d->have == LBPC_HDR_LEN) {
				if (memcmp(d->hdr, "LBPC", 4) || g16(d->hdr + 4) != LBPC_VERSION || g16(d->hdr + 6)) return fail(d, CAPT_EPROTO);
				d->phase = P_JOB_BEGIN; d->have = 0; d->in_payload = 0;
			}
			continue;
		}
		if (!d->in_payload) {   /* record header */
			take = LBPC_REC_HDR - d->have; if (take > n) take = n;
			memcpy(d->hdr + d->have, data, take); d->have += take; data += take; n -= take;
			if (d->have < LBPC_REC_HDR) continue;
			d->rtype = (uint16_t)g16(d->hdr); d->want = g32(d->hdr + 4);
			if (g16(d->hdr + 2) || d->want > LBPC_MAX_PAYLOAD) return fail(d, CAPT_EPROTO);
			if (!d->buf) { d->buf = malloc(LBPC_MAX_PAYLOAD); d->buf_cap = LBPC_MAX_PAYLOAD; if (!d->buf) return fail(d, CAPT_EIO); }
			d->have = 0; d->in_payload = 1;
			if (d->want) continue;
		} else {
			take = d->want - d->have; if (take > n) take = n;
			memcpy(d->buf + d->have, data, take); d->have += take; data += take; n -= take;
			if (d->have < d->want) continue;
		}
		{ int e = dispatch(d, d->rtype, d->buf, d->want); d->have = 0; d->in_payload = 0; if (e) return fail(d, e); }
	}
	return d->phase == P_FAILED ? d->err : 0;
}

int lbpc_drive_end(struct lbpc_drive *d)
{
	int e = d->err;
	if (d->phase == P_HEADER && d->have == 0) { e = 0; }                       /* empty stream: no job */
	else if (d->phase != P_ENDED && d->phase != P_FAILED) e = fail(d, CAPT_EDATA);   /* cut off: abort + release */
	else if (d->phase == P_ENDED && !e) {
		e = capt_job_wait_page(&d->job, d->page, 120000, report, d);
		if (e) logf_(d, "ERROR: page %u not completed (%d)", d->page, e);
		else { unsigned i; for (i = 1; i <= d->page; i++) logf_(d, "PAGE: %u 1", i); }
	}
	if (d->started) logf_(d, "STATE: -media-empty-error -media-jam-error -door-open-error -other-error -toner-empty-error -toner-low-warning -other-warning");   /* clear what this job set */
	free(d->buf); d->buf = NULL;
	if (capt_cancel_flag == &d->cancel) capt_cancel_flag = NULL;
	return e;
}

void lbpc_drive_cancel(struct lbpc_drive *d) { d->cancel = 1; }
