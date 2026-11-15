/* Convert a CUPS raster stream (stdin, via the host libcups) into the neutral "NRAS" format read by src/test/ppc/cupsshim.c.
 * Format: "NRAS", then per page 9 big-endian u32: PageSize[0], PageSize[1], Duplex, Tumble, cupsWidth, cupsHeight, cupsBitsPerPixel,
 * cupsBytesPerLine, colourspace (3 = K, 0 = W/gray), followed by cupsHeight * cupsBytesPerLine raw bytes.  Test infrastructure only. */
#include <cups/raster.h>
#include <stdio.h>
#include <stdlib.h>
static void be32(unsigned v) { putchar(v >> 24 & 255); putchar(v >> 16 & 255); putchar(v >> 8 & 255); putchar(v & 255); }
int main(void)
{
	cups_raster_t *r = cupsRasterOpen(0, CUPS_RASTER_READ);
	cups_page_header2_t h; unsigned y; unsigned char *line;
	if (!r) { fprintf(stderr, "ras2plain: not a raster stream\n"); return 1; }
	fputs("NRAS", stdout);
	while (cupsRasterReadHeader2(r, &h)) {
		be32(h.PageSize[0]); be32(h.PageSize[1]); be32(h.Duplex); be32(h.Tumble); be32(h.cupsWidth); be32(h.cupsHeight);
		be32(h.cupsBitsPerPixel); be32(h.cupsBytesPerLine); be32(h.cupsColorSpace == CUPS_CSPACE_K ? 3 : 0);
		line = malloc(h.cupsBytesPerLine);
		for (y = 0; y < h.cupsHeight; y++) {
			if (cupsRasterReadPixels(r, line, h.cupsBytesPerLine) != h.cupsBytesPerLine) { fprintf(stderr, "ras2plain: short page\n"); return 1; }
			fwrite(line, 1, h.cupsBytesPerLine, stdout);
		}
		free(line);
	}
	cupsRasterClose(r);
	return 0;
}
