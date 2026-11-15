/* Deterministic CUPS raster generator for regression baselines.
 * usage: gen_rasters PAPER BPP PAGES DUPLEX   (PAPER a4|letter|legal|a5|b5|exec, BPP 1|8, DUPLEX 0|1(long)|2(short/tumble))
 * Page content depends only on the page number: border box, diagonal, gradient stripes and a pseudo-random "text" block, so the
 * compressor sees sparse, dense and noisy areas.  1-bit pages are K (1 = black), 8-bit pages are SW (0 = black).  Writes raster to stdout. */
#include "../../cups/cups_compat.h"
#ifndef CUPS_CSPACE_SW
#define CUPS_CSPACE_SW 18   /* Tiger's CUPS 1.1 headers have no SW; the value is just stored in the header */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned rng(unsigned *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }
int main(int argc, char **argv)
{
	if (argc != 5) { fprintf(stderr, "usage: %s paper bpp pages duplex\n", argv[0]); return 2; }
	static const struct { const char *n; unsigned w, h; } P[] = { {"a4",595,842}, {"letter",612,792}, {"legal",612,1008}, {"a5",420,595}, {"b5",516,729}, {"exec",522,756} };
	unsigned pw = 0, ph = 0, i, bpp = (unsigned)atoi(argv[2]), pages = (unsigned)atoi(argv[3]), duplex = (unsigned)atoi(argv[4]), pg;
	for (i = 0; i < 6; i++) if (!strcmp(argv[1], P[i].n)) { pw = P[i].w; ph = P[i].h; }
	if (!pw || (bpp != 1 && bpp != 8)) { fprintf(stderr, "bad args\n"); return 2; }
	unsigned W = ((pw * 600 + 71) / 72 + 7) / 8 * 8, H = (ph * 600 + 36) / 72, bpl = bpp == 1 ? W / 8 : W;
	cups_raster_t *r = cupsRasterOpen(1, CUPS_RASTER_WRITE);
	unsigned char *line = malloc(bpl);
	for (pg = 1; pg <= pages; pg++) {
		cups_page_header2_t h; unsigned y, x, seed = 0x9e3779b9u * pg;
		memset(&h, 0, sizeof h);
		strcpy(h.MediaClass, "PwgRaster"); h.HWResolution[0] = h.HWResolution[1] = 600;
		h.PageSize[0] = pw; h.PageSize[1] = ph; h.ImagingBoundingBox[2] = pw; h.ImagingBoundingBox[3] = ph;
		h.cupsWidth = W; h.cupsHeight = H; h.cupsBitsPerColor = bpp; h.cupsBitsPerPixel = bpp; h.cupsBytesPerLine = bpl;
		h.cupsColorOrder = CUPS_ORDER_CHUNKED; h.cupsColorSpace = bpp == 1 ? CUPS_CSPACE_K : CUPS_CSPACE_SW; h.NumCopies = 1;
		h.Duplex = duplex != 0; h.Tumble = duplex == 2;
		cupsRasterWriteHeader2(r, &h);
		for (y = 0; y < H; y++) {
			unsigned char gray[8192]; /* 0 = black .. 255 = white, W <= 7016 */
			for (x = 0; x < W; x++) gray[x] = 255;
			if (y > 300 && y < 310 + pg) for (x = 300; x < W - 300; x++) gray[x] = 0;                       /* top rule, page dependent thickness */
			if (y > 400 && y < 800) for (x = 400; x < 1400; x++) gray[x] = (unsigned char)(((x - 400) * 255) / 1000);   /* gradient block */
			if (y > 1000 && y < 1400) for (x = 600; x < 2600; x++) gray[x] = (rng(&seed) & 7) ? 255 : 0;       /* noisy "text" block */
			for (x = 0; x < W; x++) if (x == (y * W / H) || x + 1 == (y * W / H)) gray[x] = 0;                  /* diagonal */
			if (y < 40 || y >= H - 40) for (x = 0; x < W; x++) gray[x] = 0;                                    /* top/bottom border */
			gray[0] = 0; gray[W - 1] = 0;
			if (bpp == 8) memcpy(line, gray, W);
			else { memset(line, 0, bpl); for (x = 0; x < W; x++) if (gray[x] < 128) line[x >> 3] |= (unsigned char)(0x80 >> (x & 7)); }
			cupsRasterWritePixels(r, line, bpl);
		}
	}
	free(line); cupsRasterClose(r);
	return 0;
}
