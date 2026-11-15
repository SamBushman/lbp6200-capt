/* Page producer shared by every front end of the LBP6200 filter (Linux: engine in the filter; Tiger: records for the class-driver plug-in).
 * Turns one CUPS raster page into 1-bit print lines for capt_compress_page()/capt_job_page_lines(): window offsets, 1-bit passthrough or
 * Bayer halftone of 8-bit gray, and the 180-degree rotation of the back page of a long-edge duplex pair (docs/10).  No CAPT protocol, no I/O
 * beyond reading the raster stream.  Cancellation is the core's capt_cancel_flag: once it is non-zero the remaining rows are blank. */
#ifndef RASTERPAGE_H
#define RASTERPAGE_H
#include "cups_compat.h"
#include "../core/capt.h"

struct rp_page {
	cups_raster_t *ras; cups_page_header2_t hdr; uint8_t *line;
	unsigned next_row, wbytes, y_off; int err; int rot; unsigned lines, dx; uint8_t *full;
};
enum { RP_OK = 0, RP_ENOMEM = 1, RP_ENOMEM_BACK = 2 };

/* CUPS page size in points -> paper; 0 = known, -1 = unsupported */
int rp_paper_from_pts(unsigned w, unsigned h, enum capt_paper *p);
/* per-page CAPT options from the raster header and the job options; `page` is the 1-based page number in the job */
void rp_page_opts(const cups_page_header2_t *hdr, int nopts, cups_option_t *opts, int page, struct capt_page_opts *po);
/* prepare reading page `hdr` from `ras`.  The back page of a long-edge duplex pair is read completely here (it needs the whole page). */
int rp_page_open(struct rp_page *p, cups_raster_t *ras, const cups_page_header2_t *hdr, const struct capt_page_opts *po);
/* capt_line_fn: fill one print line (print_bytes_w bytes, 1 = black) */
int rp_fetch(void *ctx, uint8_t *dst, unsigned y);
/* read and discard what the compressor did not consume (the raster stream must be positioned at the next page header) */
void rp_drain(struct rp_page *p);
void rp_close(struct rp_page *p);
#endif
