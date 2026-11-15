/* CUPS API differences between the platforms this driver builds on.
 *   Linux (CUPS 2.x) and Mac OS X 10.5+ (CUPS 1.3+): cups_page_header2_t / cupsRasterReadHeader2 exist.
 *   Mac OS X 10.4 Tiger (CUPS 1.1.23, verified in /usr/include/cups on the G5): only cups_page_header_t / cupsRasterReadHeader,
 *   no back-channel, and no CUPS_CSPACE_SW (use CUPS_CSPACE_W / _K in PPDs there).
 * Include this instead of <cups/raster.h> directly. */
#ifndef CUPS_COMPAT_H
#define CUPS_COMPAT_H
#include <cups/cups.h>
#include <cups/raster.h>
#if defined(CUPS_VERSION_MAJOR) && (CUPS_VERSION_MAJOR < 1 || (CUPS_VERSION_MAJOR == 1 && CUPS_VERSION_MINOR < 2))
typedef cups_page_header_t cups_page_header2_t;
#define cupsRasterReadHeader2(r, h) cupsRasterReadHeader((r), (h))
#endif
#endif
