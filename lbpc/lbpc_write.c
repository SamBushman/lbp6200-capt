/* LBPC record writer (filter side).  See lbpc.h.  GPL-3. */
#include <string.h>
#include "lbpc.h"

static void p16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void p32(uint8_t *p, uint32_t v) { p16(p, v & 0xFFFF); p16(p + 2, v >> 16); }

static int w_stream(struct lbpc_writer *w)
{
	uint8_t h[LBPC_HDR_LEN] = { 'L', 'B', 'P', 'C' };
	if (w->started) return 0;
	p16(h + 4, LBPC_VERSION); p16(h + 6, 0);
	w->started = 1;
	return w->write(w->ctx, h, sizeof h);
}

static int w_rec(struct lbpc_writer *w, unsigned type, const uint8_t *pl, size_t n)
{
	uint8_t h[LBPC_REC_HDR]; int e = w_stream(w);
	if (e) return e;
	p16(h, type); p16(h + 2, 0); p32(h + 4, (uint32_t)n);
	if ((e = w->write(w->ctx, h, sizeof h)) != 0) return e;
	return n ? w->write(w->ctx, pl, n) : 0;
}

int lbpc_w_job_begin(struct lbpc_writer *w, unsigned total_pages, int duplex, int tz_std, int tz_dst, const uint8_t when[8],
                     const char *host, const char *user, const char *title)
{
	uint8_t b[24 + 3 * 512]; size_t hl = strlen(host), ul = strlen(user), tl = strlen(title), o = 24;
	if (hl > 255) hl = 255;
	if (ul > 255) ul = 255;
	if (tl > 255) tl = 255;
	p32(b, total_pages); b[4] = duplex ? 1 : 0; b[5] = 0; p16(b + 6, (unsigned)tz_std & 0xFFFF); p16(b + 8, (unsigned)tz_dst & 0xFFFF);
	memcpy(b + 10, when, 8); p16(b + 18, (unsigned)hl); p16(b + 20, (unsigned)ul); p16(b + 22, (unsigned)tl);
	memcpy(b + o, host, hl); o += hl; memcpy(b + o, user, ul); o += ul; memcpy(b + o, title, tl); o += tl;
	return w_rec(w, LBPC_JOB_BEGIN, b, o);
}

static int s_begin(void *c)
{
	struct lbpc_writer *w = c; const struct capt_page_opts *o = &w->po;
	uint8_t b[8] = { (uint8_t)o->paper, (uint8_t)o->density, (uint8_t)(o->toner_save != 0), (uint8_t)(o->smooth != 0), (uint8_t)o->duplex,
	                 (uint8_t)(o->back_side != 0), (uint8_t)(o->halftone_off != 0), (uint8_t)o->media };
	return w_rec(w, LBPC_PAGE_BEGIN, b, sizeof b);
}
static int s_data(void *c, const uint8_t *d, size_t n) { return w_rec(c, LBPC_PAGE_DATA, d, n); }
static int s_end(void *c) { return w_rec(c, LBPC_PAGE_END, NULL, 0); }

int lbpc_w_page(struct lbpc_writer *w, const struct capt_page_opts *po, capt_line_fn fetch, void *fctx)
{
	struct capt_page_sink sk; sk.begin = s_begin; sk.data = s_data; sk.end = s_end; sk.ctx = w;
	w->po = *po;
	return capt_compress_page(po, fetch, fctx, &sk);
}

int lbpc_w_job_end(struct lbpc_writer *w, unsigned state)
{
	uint8_t b[1]; b[0] = (uint8_t)state;
	return w_rec(w, LBPC_JOB_END, b, 1);
}
