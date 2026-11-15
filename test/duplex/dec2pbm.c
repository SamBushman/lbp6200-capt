#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int (*dec_fn)(int *srcptr, int dst, int linesize, int nlines, int *srclen, int zero, unsigned char *params);
int main(int argc, char **argv)
{
	void *h = dlopen(argv[1], RTLD_NOW); dec_fn dec = (dec_fn)dlsym(h, "lCaptDecode");
	FILE *f = fopen(argv[2], "rb"); static unsigned char in[400000]; int n = fread(in, 1, sizeof in, f) - 4;
	int ls = atoi(argv[3]), total = atoi(argv[4]); const int BL = 256, PRE = 16;
	unsigned char params[8] = { 1, 4, 1, 1, 0, 0xf0, 0, 0 };
	unsigned char *img = calloc(1, (size_t)ls * total);
	int src = (int)(size_t)(in + 4), srclen = n, line = 0;
	while (line < total) {
		int nl = total - line < BL ? total - line : BL;
		unsigned char *buf = calloc(1, (size_t)ls * (nl + PRE));
		int r = dec(&src, (int)(size_t)(buf + ls * PRE), ls, nl, &srclen, 0, params);
		if (r != nl) { fprintf(stderr, "band ret=%d want %d at line %d\n", r, nl, line); break; }
		memcpy(img + (size_t)ls * line, buf + ls * PRE, (size_t)ls * nl); free(buf); line += nl;
	}
	fprintf(stderr, "decoded %d lines\n", line);
	FILE *o = fopen(argv[5], "wb"); fprintf(o, "P4\n%d %d\n", ls * 8, total); fwrite(img, 1, (size_t)ls * total, o); fclose(o);
	return 0;
}
