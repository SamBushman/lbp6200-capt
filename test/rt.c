/* Round trip: decode real wire stream (Canon decoder) -> bitmap -> recompress with captdriver hiscoa -> byte compare */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hiscoa-compress.h"
typedef int (*dec_fn)(int *srcptr, int dst, int linesize, int nlines, int *srclen, int zero, unsigned char *params);
int main(int argc, char **argv)
{
	void *h = dlopen(argv[1], RTLD_NOW); dec_fn dec = (dec_fn)dlsym(h, "lCaptDecode");
	FILE *f = fopen(argv[2], "rb"); static unsigned char in[200000]; int n = fread(in, 1, sizeof in, f) - 4;
	unsigned char params[8] = { 1, 4, 1, 1, 0, 0xf0, 0, 0 };
	const int ls = 592, total = 6778, BL = 256, PRE = 16;
	unsigned char *img = calloc(1, (size_t)ls * total);
	int src = (int)(size_t)(in + 4), srclen = n, line = 0, bands = 0;
	size_t bandstart[64]; 
	while (line < total) {
		int nl = total - line < BL ? total - line : BL;
		unsigned char *buf = calloc(1, (size_t)ls * (nl + PRE));
		bandstart[bands] = (unsigned char*)(size_t)src - (in + 4);
		int r = dec(&src, (int)(size_t)(buf + ls * PRE), ls, nl, &srclen, 0, params);
		if (r != nl) { printf("band %d decode ret=%d (want %d) at stream off %zu\n", bands, r, nl, bandstart[bands]); break; }
		memcpy(img + (size_t)ls * line, buf + ls * PRE, (size_t)ls * nl); free(buf);
		line += nl; bands++;
	}
	printf("decoded %d bands, %d lines, stream consumed %d of %d (remaining %d)\n", bands, line, (int)((unsigned char*)(size_t)src - (in + 4)), n, srclen);
	bandstart[bands] = (unsigned char*)(size_t)src - (in + 4);
	/* recompress */
	struct hiscoa_params hp = { 1, 4, 0, -16, 0 };
	size_t off = 0; int same = 0;
	for (int b = 0; b < bands; b++) {
		int l0 = b * BL, nl = total - l0 < BL ? total - l0 : BL;
		unsigned char out[200000];
		for (int eob = 0; eob < 2; eob++) {
			size_t k = hiscoa_compress_band(out, sizeof out, img + (size_t)ls * l0, ls, nl, eob ? HISCOA_EOB_LAST : HISCOA_EOB_NORMAL, &hp);
			size_t wl = bandstart[b + 1] - bandstart[b];
			if (k == wl && !memcmp(out, in + 4 + bandstart[b], k)) { same++; if (b < 3 || b == bands - 1) printf("band %d: IDENTICAL to Canon wire (%zu bytes, eob=%d)\n", b, k, eob); break; }
			else if (eob) printf("band %d: differs (ours %zu wire %zu)\n", b, k, wl);
		}
		off += 0;
	}
	printf("%d/%d bands byte-identical\n", same, bands);
	{ int ok = 0, tot_ours = 0;
	  for (int b = 0; b < bands; b++) {
		int l0 = b * BL, nl = total - l0 < BL ? total - l0 : BL;
		static unsigned char out[200000]; unsigned char *buf = calloc(1, (size_t)ls * (nl + PRE));
		size_t k = hiscoa_compress_band(out, sizeof out, img + (size_t)ls * l0, ls, nl, HISCOA_EOB_NORMAL, &hp);
		tot_ours += k;
		int s2 = (int)(size_t)out, sl = (int)k;
		int r = dec(&s2, (int)(size_t)(buf + ls * PRE), ls, nl, &sl, 0, params);
		if (r == nl && !memcmp(buf + ls * PRE, img + (size_t)ls * l0, (size_t)ls * nl)) ok++;
		free(buf);
	  }
	  printf("our recompressed bands decoded by Canon to identical image: %d/%d (ours total %d vs wire %d)\n", ok, bands, tot_ours, n);
	}
	return 0;
}
