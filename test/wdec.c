/* Decode a real wire band stream with Canon's lCaptDecode; report lines decoded. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int (*dec_fn)(int *srcptr, int dst, int linesize, int nlines, int *srclen, int zero, unsigned char *params);
int main(int argc, char **argv)
{
	void *h = dlopen(argv[1], RTLD_NOW); dec_fn dec = (dec_fn)dlsym(h, "lCaptDecode");
	FILE *f = fopen(argv[2], "rb"); static unsigned char in[200000]; int n = fread(in, 1, sizeof in, f) - 4;
	unsigned char params[8] = { 1, 4, 1, 1, 0, 0xf0, 0, 0 };
	int ls = atoi(argv[3]), nl = atoi(argv[4]);
	unsigned char *out = calloc(1, (size_t)ls * (nl + 32));
	int src = (int)(size_t)(in + 4), srclen = n;
	int r = dec(&src, (int)(size_t)(out + ls * 16), ls, nl, &srclen, 0, params);
	printf("ret=%d consumed=%d of %d remaining srclen=%d\n", r, (int)((unsigned char*)(size_t)src - (in + 4)), n, srclen);
	long nz = 0; for (long i = 0; i < (long)ls * nl; i++) nz += out[ls*16+i] != 0; printf("nonzero bytes %ld\n", nz);
	return 0;
}
