/* Portable CAPT 3.0 (Canon LBP6200) protocol core.  C99, no OS calls, endian-clean.
 * All platform access goes through struct capt_io. GPL-3 (links hiscoa from captdriver). */
#ifndef CAPT_H
#define CAPT_H
#include <stddef.h>
#include <stdint.h>

/* ---- I/O callbacks (supplied by the platform shim) ---- */
struct capt_io {
	void *ctx;
	/* write exactly n bytes; return 0 on success, <0 on error */
	int (*write)(void *ctx, const uint8_t *buf, size_t n);
	/* read up to n bytes (blocking up to timeout_ms); return bytes read, 0 on timeout, <0 on error */
	int (*read)(void *ctx, uint8_t *buf, size_t n, int timeout_ms);
	void (*sleep_ms)(void *ctx, unsigned ms);
};

/* ---- command codes (Canon names from libcncaptnpm) ---- */
enum capt_cmd {
	PC_GET_STATUS       = 0xA0A0, PC_GET_EXT_STATUS2 = 0xA0A8, PC_GET_CARTRIDGE = 0xA0A4,
	PC_GET_PAGE_STATUS  = 0xA0A1, PC_GET_ENGINE_VER  = 0xA1A8, PC_GET_PRINTER_INFO = 0xA1A1,
	PC_GET_TIME         = 0xA3AF, PC_RESERVE_UNIT    = 0xA2A0, PCR_RELEASE_UNIT = 0xE0A9,
	PCR_SET_LED         = 0xE1A2, PCR_SET_POWER_DOWN = 0xE0BA, PCR_SET_JOB_INFO = 0xE1A1,
	PCR_CLEAR_MISPRINT  = 0xE0A3, PCR_UNKNOWN_E0A2   = 0xE0A2, PCR_DISCARD_DATA = 0xE0A4,
	PCR_GO_ONLINE       = 0xE0A5, PCR_SET_PRINTED_PAGES = 0xE1A6, PCR_START_PRINT = 0xE0A7,
	IC_COMPOUND_DATA    = 0xD0A9, IC_PAGE_PARAMS = 0xD0A0, IC_COMP_PARAMS = 0xD0A4,
	IC_D0A1             = 0xD0A1, IC_D0A2        = 0xD0A2, IC_VIDEO_DATA = 0xC0A0, IC_BLACK_END = 0xC0A4
};

enum { CAPT_OK = 0, CAPT_EIO = -1, CAPT_ETIMEOUT = -2, CAPT_EPROTO = -3, CAPT_ESTATUS = -4, CAPT_EARG = -5 };
#define CAPT_MAX_PKT 0xFFFF

/* ---- wire helpers ---- */
static inline void capt_put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void capt_put32(uint8_t *p, uint32_t v) { capt_put16(p, v & 0xFFFF); capt_put16(p + 2, v >> 16); }
static inline unsigned capt_get16(const uint8_t *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }

/* build packet [cmd][size][payload]; returns total size, 0 if it does not fit */
size_t capt_pkt(uint8_t *out, size_t cap, unsigned cmd, const uint8_t *payload, size_t n);
/* send a command; if reply!=NULL read the reply (header-checked) into it. returns reply length or <0 */
int capt_xact(struct capt_io *io, unsigned cmd, const uint8_t *payload, size_t n,
              uint8_t *reply, size_t reply_cap, int timeout_ms);

/* ---- status ---- */
struct capt_status {
	unsigned st[7];                 /* STATUS0..6 */
	unsigned page_decoding, page_printing, page_out, page_completed, page_received;
	unsigned job_no, pages_in_job, pages_printed, event_counter;
	unsigned eng_online_cmds;
};
enum capt_state {                    /* user-visible summary, maps onto Canon msgtable keys */
	CAPT_ST_READY, CAPT_ST_PRINTING, CAPT_ST_BUSY, CAPT_ST_NO_PAPER, CAPT_ST_ERROR, CAPT_ST_UNINIT, CAPT_ST_POWERUP
};
int capt_get_status(struct capt_io *io, struct capt_status *s);
enum capt_state capt_status_summary(const struct capt_status *s);
const char *capt_state_name(enum capt_state s);
/* A0A4 cartridge array. returns entry count or <0 */
struct capt_cartridge { uint8_t flag, id, flag2; uint8_t raw[12]; };
int capt_get_cartridges(struct capt_io *io, struct capt_cartridge *out, int max);

/* ---- page / job description ---- */
enum capt_paper { CAPT_A4, CAPT_LETTER, CAPT_LEGAL, CAPT_A5, CAPT_B5, CAPT_EXECUTIVE, CAPT_PAPER_COUNT };
enum capt_media { CAPT_PLAIN = 1, CAPT_THICK = 2, CAPT_ROUGH1 = 9, CAPT_ROUGH2 = 9, CAPT_OHP = 0x13,
	CAPT_LABELS = 0x14, CAPT_ENVELOPE = 0x1c, CAPT_PLAIN_L = 0x24 };
enum capt_duplex { CAPT_SIMPLEX = 0, CAPT_DUPLEX_LONG = 1, CAPT_DUPLEX_SHORT = 2 };
struct capt_page_opts {
	enum capt_paper paper;
	unsigned density;      /* 1..16, default 4 (CNTonerDensity) */
	int toner_save;        /* draft mode */
	int smooth;            /* image refinement, default 1 */
	enum capt_duplex duplex;
	int back_side;         /* even page of a duplex job */
	int halftone_off;      /* CNHalftone=None(solid) */
	enum capt_media media; /* 0 = plain */
};
void capt_page_opts_default(struct capt_page_opts *o);
/* geometry @600 dpi. The printable window starts CAPT_X_OFF px from the left and CAPT_Y_OFF px from the top of the page
 * (measured: edge-bar test, docs/06; A4 top is 120 but 118 elsewhere - 2px, ignored). */
#define CAPT_X_OFF 120
/* per-paper top offset is capt_geom.y_off */
struct capt_geom { unsigned px_w, px_h, print_bytes_w, print_lines, y_off; };
int capt_paper_geom(enum capt_paper p, struct capt_geom *g);
/* builders (exposed for tests) */
size_t capt_build_d0a0(uint8_t *out, const struct capt_page_opts *o);   /* 0x48 payload incl. hdr -> returns 72? see .c */
size_t capt_build_job_info(uint8_t *out, unsigned phase, unsigned state, const uint8_t when[8], const char *name_ascii);
size_t capt_build_compound(uint8_t *out, size_t cap, const struct capt_page_opts *o);

/* ---- job engine ---- */
struct capt_job {
	struct capt_io *io;
	unsigned reserve_id;
	uint8_t when[8];                 /* date/time stamp (u16 year, mon, day, hour, min, sec, ?) from caller */
	char name[24];
	struct capt_page_opts opts;
};
int capt_job_begin(struct capt_job *j, struct capt_io *io, const uint8_t when[8], const char *name);
/* send one page: band streams (already Hi-SCoA compressed, each <= CAPT_MAX_PKT-4) */
int capt_job_page(struct capt_job *j, const struct capt_page_opts *o,
                  const uint8_t *const *bands, const size_t *band_len, int nbands);
int capt_job_end(struct capt_job *j);

/* poll A0A8 until page `pageno` (1-based) has been delivered (page_out/page_completed >= pageno), an error state
 * appears, or timeout. `cb` (optional) is called with every status sample (for STATE: reporting). returns 0 ok, <0 err/timeout */
int capt_job_wait_page(struct capt_job *j, unsigned pageno, unsigned timeout_ms,
                       void (*cb)(void *, const struct capt_status *), void *cbctx);
/* wait until the printer can take data (not buffer-full) */
int capt_wait_ready_for_data(struct capt_io *io, unsigned timeout_ms);

/* streaming page API: begin -> data (any chunking, split into C0A0 <=0xff00) -> end */
int capt_page_begin(struct capt_job *j, const struct capt_page_opts *o);
int capt_page_data(struct capt_job *j, const uint8_t *d, size_t n);
int capt_page_end(struct capt_job *j);
/* rasterised page: fetch(ctx, dst, y) must fill one 1bpp line of print_bytes_w bytes (1 = black); lines 0..print_lines-1.
 * Compresses in 256-line Hi-SCoA bands (as Canon does) and streams the result. */
typedef int (*capt_line_fn)(void *ctx, uint8_t *dst, unsigned y);
#define CAPT_BAND_LINES 256
int capt_job_page_lines(struct capt_job *j, const struct capt_page_opts *o, capt_line_fn fetch, void *ctx);
#endif
