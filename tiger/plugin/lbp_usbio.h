/* capt_io over the bulk pipes of an IOUSBInterfaceInterface182 (shared by the class-driver plug-in and the read-only probe tool).  GPL-3. */
#ifndef LBP_USBIO_H
#define LBP_USBIO_H
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <string.h>
#include <unistd.h>
#include "../../core/capt.h"
#include "../../lbpc/lbpc.h"

#define RXCAP 4096
struct Priv {
	IOUSBInterfaceInterface182 **i182;      /* the same interface, queried for the *TO calls (ReadPipeTO needs 10.2.3+) */
	UInt8 out, in;
	uint8_t rx[RXCAP]; size_t rxn, rxo;       /* bytes read from the printer but not yet consumed by the engine (it reads header and payload separately) */
	struct capt_io io; struct lbpc_drive drive; int drive_ready;
};

/* ---------- capt_io over the bulk pipes ---------- */
static IOReturn pipe_clear(struct Priv *p, UInt8 pipe)
{
	IOUSBInterfaceInterface182 *i = *p->i182;
	return i->ClearPipeStall ? i->ClearPipeStall(p->i182, pipe) : kIOReturnUnsupported;
}

static int io_write_frame(struct Priv *p, const uint8_t *b, size_t n)
{
	IOReturn r = (*p->i182)->WritePipe(p->i182, p->out, (void *)b, (UInt32)n);
#ifdef LBP_TRACE
	{ FILE *f = fopen(LBP_TRACE, "a"); if (f) { fprintf(f, "%ld W %02x%02x len=%lu r=%x%s\n", (long)time(NULL), n > 1 ? b[0] : 0, n > 1 ? b[1] : 0, (unsigned long)n, (unsigned)r, n >= 14 && b[0] == 0xa9 ? " D0A9 type" : ""); if (n >= 14 && b[0] == 0xa9 && b[1] == 0xd0) fprintf(f, "   D0A0[5]=%02x\n", b[13]); fclose(f); } }
#endif
	if (r == kIOUSBPipeStalled) { pipe_clear(p, p->out); r = (*p->i182)->WritePipe(p->i182, p->out, (void *)b, (UInt32)n); }
	return r == kIOReturnSuccess ? 0 : -1;
}
/* the printer wants one CAPT command per USB transfer; the engine hands over single frames, except a held duplex front page (several frames in one buffer) */
static int io_write(void *c, const uint8_t *b, size_t n)
{
	struct Priv *p = c;
	while (n) {
		size_t sz = n;
		if (n >= 4) { size_t f = capt_get16(b + 2); if (f >= 4 && f <= n) sz = f; }
		if (io_write_frame(p, b, sz)) return -1;
		b += sz; n -= sz;
	}
	return 0;
}
static int io_read(void *c, uint8_t *b, size_t n, int timeout_ms)
{
	struct Priv *p = c;
	if (p->rxo == p->rxn) {
		UInt32 got = RXCAP; IOReturn r;
		p->rxo = p->rxn = 0;
		r = (*p->i182)->ReadPipeTO(p->i182, p->in, p->rx, &got, (UInt32)timeout_ms, (UInt32)timeout_ms);
		if (r == kIOUSBPipeStalled) { pipe_clear(p, p->in); return 0; }
		/* On a timeout ReadPipeTO leaves *size at the buffer size (seen on the real printer: 4096 bytes of leftover buffer were taken for data) - never trust it then. */
		/* An aborted bulk-in transfer can leave the data toggle out of step (Apple's ReadPipeTO docs; on the real printer the next reply came back garbled) -> resynchronise. */
		if (r == kIOUSBTransactionTimeout || r == kIOReturnTimeout) { pipe_clear(p, p->in); return 0; }
		if (r != kIOReturnSuccess) return -1;
		p->rxn = got;
		if (!got) return 0;
	}
	{ size_t k = p->rxn - p->rxo; if (k > n) k = n; memcpy(b, p->rx + p->rxo, k); p->rxo += k; return (int)k; }
}
static void io_sleep(void *c, unsigned ms) { (void)c; usleep(ms * 1000u); }
#endif
