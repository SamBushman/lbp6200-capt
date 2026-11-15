/* LBPC: the private record stream between the Tiger raster filter (rastertolbpc) and the class-driver plug-in that owns the USB pipe.
 *
 * Why: Tiger's CUPS 1.1.23 has no back-channel, and Apple's usb backend only moves bytes, so the CAPT conversation (which needs replies) has to
 * run inside the USB class driver plug-in.  The filter stays a pure transform (raster -> compressed page data); the plug-in replays the records
 * into the CAPT job engine (core/capt.c) - the same engine the Linux filter drives directly.  See docs/13-tiger-port-design.md.
 *
 * Byte order is little-endian everywhere (like CAPT itself), independent of the host, so a filter built for PPC and a driver built for x86 agree.
 *
 *   stream  := "LBPC" u16 version(=1) u16 reserved(=0)  record*
 *   record  := u16 type  u16 reserved(=0)  u32 length  payload[length]
 *
 *   1 JOB_BEGIN   u32 total_pages (0 = unknown)  u8 duplex(0/1)  u8 reserved  s16 tz_std  s16 tz_dst  u8 when[8]
 *                 u16 host_len  u16 user_len  u16 title_len  then the three strings (no NUL; the engine truncates them to what E1A1 holds)
 *                 tz_* = minutes west of UTC (standard / daylight), when = capt year(u16) mon day hour min sec 3
 *   2 PAGE_BEGIN  u8 paper  u8 density  u8 toner_save  u8 smooth  u8 duplex  u8 back_side  u8 halftone_off  u8 media      (struct capt_page_opts)
 *   3 PAGE_DATA   the next chunk of the compressed page stream, 1..0xff00 bytes, exactly the chunking capt_compress_page() produces
 *                 (each chunk becomes one C0A0 frame, so the boundaries are wire format)
 *   4 PAGE_END    (empty)
 *   5 JOB_END     u8 state: 6 = all data delivered, 5 = input ended early (page was completed), 4 = cancelled, 2 = filter failed
 *
 * Order: JOB_BEGIN, then (PAGE_BEGIN PAGE_DATA+ PAGE_END)*, then JOB_END.  Anything else is a protocol error.
 * A stream that ends without JOB_END aborts the job (the printer is released, a final E1A1 only if the link is healthy). */
#ifndef LBPC_H
#define LBPC_H
#include <stddef.h>
#include <stdint.h>
#include "../core/capt.h"

enum { LBPC_VERSION = 1, LBPC_HDR_LEN = 8, LBPC_REC_HDR = 8, LBPC_MAX_PAYLOAD = 0x10000 };
enum { LBPC_JOB_BEGIN = 1, LBPC_PAGE_BEGIN = 2, LBPC_PAGE_DATA = 3, LBPC_PAGE_END = 4, LBPC_JOB_END = 5 };
enum { LBPC_END_FAILED = 2, LBPC_END_CANCELLED = 4, LBPC_END_INCOMPLETE = 5, LBPC_END_DONE = 6 };

/* ---------------- writer (filter side) ---------------- */
struct lbpc_writer {
	void *ctx;
	int (*write)(void *ctx, const uint8_t *b, size_t n);   /* all n bytes or <0 */
	int started;                                            /* stream header written */
	struct capt_page_opts po;                               /* set by lbpc_w_page(); used by the sink */
};
int lbpc_w_job_begin(struct lbpc_writer *w, unsigned total_pages, int duplex, int tz_std, int tz_dst, const uint8_t when[8],
                     const char *host, const char *user, const char *title);
/* one page: PAGE_BEGIN, the compressed chunks of capt_compress_page(), PAGE_END */
int lbpc_w_page(struct lbpc_writer *w, const struct capt_page_opts *po, capt_line_fn fetch, void *fctx);
int lbpc_w_job_end(struct lbpc_writer *w, unsigned state);

/* ---------------- driver (class-driver plug-in side) ---------------- */
/* Receives the byte stream in pieces of any size (the usb backend hands over 2048-byte chunks), replays it into the CAPT engine.
 * `log` gets finished CUPS back-channel lines ("STATE: ...", "INFO: ...", "ERROR: ...", "PAGE: n 1"), the plug-in writes them to stderr. */
struct lbpc_drive {
	struct capt_io *io;
	void (*log)(void *ctx, const char *line); void *logctx;
	/* --- internal --- */
	int phase;                 /* 0 header, 1 expect JOB_BEGIN, 2 between pages, 3 in page, 4 job ended, 9 failed */
	uint8_t hdr[LBPC_REC_HDR]; size_t have; uint32_t want; int in_payload; uint16_t rtype;
	uint8_t *buf; size_t buf_cap;
	struct capt_job job; int started, finished; unsigned page; unsigned end_state; int err;
	struct capt_page_opts po;
	volatile int cancel;       /* set by lbpc_drive_cancel(); the engine polls it (capt_cancel_flag) */
	int rep_last; long rep_lastc;   /* status reporting: last STATE key, time of the last cartridge refresh */
};
void lbpc_drive_init(struct lbpc_drive *d, struct capt_io *io, void (*log)(void *, const char *), void *logctx);
/* feed stream bytes; returns 0, or a negative CAPT_E* once the stream/job failed (further input is ignored; the printer was already released) */
int lbpc_drive_feed(struct lbpc_drive *d, const uint8_t *data, size_t n);
/* end of input (the backend closed stdin / Close()): a complete job -> wait until the printer reports every page delivered and emit PAGE: lines;
 * an incomplete one -> abort (release).  Returns 0 or a negative error.  Frees internal buffers; safe to call once. */
int lbpc_drive_end(struct lbpc_drive *d);
/* asynchronous cancel (Abort()): only raises the flag the engine polls (safe from another thread or a signal handler); the next lbpc_drive_feed()
 * or lbpc_drive_end() then aborts the job with CAPT_ECANCEL (release, final E1A1 4) and returns that error. */
void lbpc_drive_cancel(struct lbpc_drive *d);
#endif
