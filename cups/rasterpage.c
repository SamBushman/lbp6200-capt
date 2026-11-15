/* Page producer shared by the LBP6200 filter front ends.  See rasterpage.h.  GPL-3. */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "rasterpage.h"

int rp_paper_from_pts(unsigned w, unsigned h, enum capt_paper *p)
{
	static const struct { unsigned w, h; enum capt_paper p; } t[] = {
		{ 595, 842, CAPT_A4 }, { 612, 792, CAPT_LETTER }, { 612, 1008, CAPT_LEGAL },
		{ 420, 595, CAPT_A5 }, { 516, 729, CAPT_B5 }, { 522, 756, CAPT_EXECUTIVE } };
	unsigned i;
	for (i = 0; i < sizeof t / sizeof *t; i++)
		if ((w > t[i].w ? w - t[i].w : t[i].w - w) <= 3 && (h > t[i].h ? h - t[i].h : t[i].h - h) <= 3) { *p = t[i].p; return 0; }
	return -1;
}

static const uint8_t bayer[8][8] = {
	{ 0, 32, 8, 40, 2, 34, 10, 42 }, { 48, 16, 56, 24, 50, 18, 58, 26 }, { 12, 44, 4, 36, 14, 46, 6, 38 }, { 60, 28, 52, 20, 62, 30, 54, 22 },
	{ 3, 35, 11, 43, 1, 33, 9, 41 }, { 51, 19, 59, 27, 49, 17, 57, 25 }, { 15, 47, 7, 39, 13, 45, 5, 37 }, { 63, 31, 55, 23, 61, 29, 53, 21 } };

static int opt_int(int n, cups_option_t *o, const char *k, int def)
{ const char *v = cupsGetOption(k, n, o); return v ? atoi(v) : def; }

void rp_page_opts(const cups_page_header2_t *hdr, int nopts, cups_option_t *opts, int page, struct capt_page_opts *po)
{
	const char *be = cupsGetOption("BindEdge", nopts, opts);
	capt_page_opts_default(po);
	rp_paper_from_pts((unsigned)hdr->PageSize[0], (unsigned)hdr->PageSize[1], &po->paper);   /* callers check support first */
	po->density = (unsigned)opt_int(nopts, opts, "CNTonerDensity", 4);
	{ const char *d = cupsGetOption("CNDraftMode", nopts, opts); po->toner_save = d && (!strcasecmp(d, "True") || !strcasecmp(d, "on")); }
	{ const char *d = cupsGetOption("CNSuperSmooth", nopts, opts); po->smooth = !(d && !strcasecmp(d, "False")); }
	if (hdr->Duplex) po->duplex = (hdr->Tumble || (be && !strcasecmp(be, "Top"))) ? CAPT_DUPLEX_SHORT : CAPT_DUPLEX_LONG;
	po->back_side = hdr->Duplex && (page % 2 == 0);
}

static void read_row(struct rp_page *p)
{
	if (capt_cancel_flag && *capt_cancel_flag) memset(p->line, p->hdr.cupsColorSpace == CUPS_CSPACE_K ? 0 : 0xff, p->hdr.cupsBytesPerLine);   /* cancelled: no more input, blank rest of the page */
	else if (p->next_row < p->hdr.cupsHeight) {
		if (cupsRasterReadPixels(p->ras, p->line, p->hdr.cupsBytesPerLine) != p->hdr.cupsBytesPerLine) { p->err = 1; memset(p->line, 0xff, p->hdr.cupsBytesPerLine); }
	} else memset(p->line, p->hdr.cupsColorSpace == CUPS_CSPACE_K ? 0 : 0xff, p->hdr.cupsBytesPerLine);
	p->next_row++;
}

/* pixel of the 8-bit/1-bit source raster at (row, col) as 0 = black .. 255 = white */
static unsigned src_pixel(const struct rp_page *p, const uint8_t *line, unsigned col)
{
	int black_is_one = p->hdr.cupsColorSpace == CUPS_CSPACE_K;
	unsigned v;
	if (col >= p->hdr.cupsWidth) return 255;
	if (p->hdr.cupsBitsPerPixel == 1) { v = (line[col >> 3] >> (7 - (col & 7))) & 1; return (black_is_one ? v : !v) ? 0 : 255; }
	v = line[col]; return black_is_one ? 255 - v : v;
}

int rp_fetch(void *ctx, uint8_t *dst, unsigned y)
{
	struct rp_page *p = ctx; unsigned row = y + p->y_off, x;
	int black_is_one = p->hdr.cupsColorSpace == CUPS_CSPACE_K;
	if (p->rot) {
		/* Canon back page of a long-edge duplex pair (docs/10): rotate the RASTER by 180 deg inside the printable window, shifted dx bits towards
		 * column 0, then halftone with the normal screen (Canon rotates before halftoning).  B[r'][c'] = F[Lines-1-r'][NB-1-dx-c'] */
		unsigned nb = p->wbytes * 8, srow = p->y_off + (p->lines - 1 - y);
		const uint8_t *line = p->full + (size_t)srow * p->hdr.cupsBytesPerLine;
		memset(dst, 0, p->wbytes);
		if (srow >= p->hdr.cupsHeight) return 0;
		for (x = 0; x + p->dx < nb; x++) {
			unsigned v = src_pixel(p, line, CAPT_X_OFF + (nb - 1 - p->dx - x));
			if (p->hdr.cupsBitsPerPixel == 1 ? v == 0 : v < (unsigned)bayer[row & 7][(x + CAPT_X_OFF) & 7] * 4 + 2) dst[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
		}
		return 0;
	}
	while (p->next_row <= row) read_row(p);
	memset(dst, 0, p->wbytes);
	if (p->hdr.cupsBitsPerPixel == 1) {
		unsigned off = CAPT_X_OFF / 8, avail = p->hdr.cupsBytesPerLine > off ? p->hdr.cupsBytesPerLine - off : 0;
		unsigned n = avail < p->wbytes ? avail : p->wbytes;
		for (x = 0; x < n; x++) dst[x] = black_is_one ? p->line[off + x] : (uint8_t)~p->line[off + x];
	} else if (p->hdr.cupsBitsPerPixel == 8) {
		for (x = 0; x < p->wbytes * 8 && x + CAPT_X_OFF < p->hdr.cupsWidth; x++) {
			unsigned v = p->line[x + CAPT_X_OFF]; if (black_is_one) v = 255 - v;     /* v: 0 = black .. 255 = white */
			if (v < (unsigned)bayer[row & 7][(x + CAPT_X_OFF) & 7] * 4 + 2) dst[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
		}
	}
	return 0;
}

int rp_page_open(struct rp_page *p, cups_raster_t *ras, const cups_page_header2_t *hdr, const struct capt_page_opts *po)
{
	struct capt_geom g;
	capt_paper_geom(po->paper, &g);
	memset(p, 0, sizeof *p); p->ras = ras; p->hdr = *hdr; p->wbytes = g.print_bytes_w; p->y_off = g.y_off;
	p->line = malloc(hdr->cupsBytesPerLine + 8);
	if (!p->line) return RP_ENOMEM;
	if (po->back_side && po->duplex == CAPT_DUPLEX_LONG) {          /* docs/10: back page = 180 deg rotated raster; needs the whole page */
		unsigned r;
		p->rot = 1; p->lines = g.print_lines; p->dx = g.back_dx;
		p->full = malloc((size_t)hdr->cupsBytesPerLine * hdr->cupsHeight);
		if (!p->full) { free(p->line); p->line = NULL; return RP_ENOMEM_BACK; }
		for (r = 0; r < hdr->cupsHeight; r++) {
			if (cupsRasterReadPixels(ras, p->full + (size_t)r * hdr->cupsBytesPerLine, hdr->cupsBytesPerLine) != hdr->cupsBytesPerLine) { p->err = 1; memset(p->full + (size_t)r * hdr->cupsBytesPerLine, 0xff, hdr->cupsBytesPerLine); }
		}
		p->next_row = hdr->cupsHeight;
	}
	return RP_OK;
}

void rp_drain(struct rp_page *p) { while (p->next_row < p->hdr.cupsHeight) read_row(p); }

void rp_close(struct rp_page *p) { free(p->line); free(p->full); p->line = NULL; p->full = NULL; }
