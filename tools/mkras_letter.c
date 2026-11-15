/* Letter 600dpi 1-bit CUPS raster with text-like content (pseudo-random glyph blocks) so the compressed page is tens of KB like CUPS' test page.
 * usage: mkras_letter [density_percent] */
#include <cups/raster.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
	unsigned W = 5100, H = 6600, bpl = W / 8, y, x, dens = argc > 1 ? (unsigned)atoi(argv[1]) : 30;
	cups_raster_t *r = cupsRasterOpen(1, CUPS_RASTER_WRITE);
	cups_page_header2_t h; unsigned char *line = malloc(bpl);
	memset(&h, 0, sizeof h);
	strcpy(h.MediaClass, "PwgRaster"); h.HWResolution[0] = h.HWResolution[1] = 600;
	h.PageSize[0] = 612; h.PageSize[1] = 792; h.ImagingBoundingBox[2] = 612; h.ImagingBoundingBox[3] = 792;
	h.cupsWidth = W; h.cupsHeight = H; h.cupsBitsPerColor = 1; h.cupsBitsPerPixel = 1; h.cupsBytesPerLine = bpl;
	h.cupsColorOrder = CUPS_ORDER_CHUNKED; h.cupsColorSpace = CUPS_CSPACE_K; h.NumCopies = 1;
	cupsRasterWriteHeader2(r, &h);
	srand(1234);
	for (y = 0; y < H; y++) {
		memset(line, 0, bpl);
		if ((y / 60) % 3 != 2 && y > 600 && y < 5800)            /* text lines: 60-row bands, every third is blank */
			for (x = 600 / 8; x < (W - 600) / 8; x++) if ((unsigned)(rand() % 100) < dens && ((x / 3) % 4) != 3) line[x] = (unsigned char)rand();
		cupsRasterWritePixels(r, line, bpl);
	}
	cupsRasterClose(r); return 0;
}
