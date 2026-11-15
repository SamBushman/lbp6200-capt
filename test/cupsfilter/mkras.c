/* make a 1-page A4 600dpi 1-bit K CUPS raster with the same bars as scratch/sweep/edge.ps */
#include <cups/raster.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
	int pages = argc > 1 ? atoi(argv[1]) : 1, p;
	cups_raster_t *r = cupsRasterOpen(1, CUPS_RASTER_WRITE);
	for (p = 0; p < pages; p++) {
		cups_page_header2_t h; unsigned y, x; unsigned W = 4960, H = 7016, bpl = W / 8; unsigned char *line = malloc(bpl);
		memset(&h, 0, sizeof h);
		strcpy(h.MediaClass, "PwgRaster"); h.HWResolution[0] = h.HWResolution[1] = 600;
		h.PageSize[0] = 595; h.PageSize[1] = 842; h.ImagingBoundingBox[2] = 595; h.ImagingBoundingBox[3] = 842;
		h.cupsWidth = W; h.cupsHeight = H; h.cupsBitsPerColor = 1; h.cupsBitsPerPixel = 1; h.cupsBytesPerLine = bpl;
		h.cupsColorOrder = CUPS_ORDER_CHUNKED; h.cupsColorSpace = CUPS_CSPACE_K; h.NumCopies = 1; h.Duplex = argc > 2 ? atoi(argv[2]) : 0;
		cupsRasterWriteHeader2(r, &h);
		for (y = 0; y < H; y++) {
			memset(line, 0, bpl);
			if (y >= 833 && y <= 850) memset(line, 0xff, bpl);
			for (x = 833; x <= 849; x++) line[x >> 3] |= (unsigned char)(0x80 >> (x & 7));
			cupsRasterWritePixels(r, line, bpl);
		}
		free(line);
	}
	cupsRasterClose(r); return 0;
}
