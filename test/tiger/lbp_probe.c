/* READ-ONLY probe of the real printer on Tiger (step 4a): opens the printer-class interface, queries status/cartridges/device-ID with the same pipe code the plug-in uses
 * (tiger/plugin/lbp_usbio.h), checks how ReadPipeTO behaves with the real device (timeout with nothing pending, pipe still usable afterwards) and closes again.
 * Only GET commands are sent (A0A8, A0A4, A1A0, A3AF as the Linux driver's idle polling does) - no reserve, no job, no data.  usage: lbp_probe [rounds] */
#include <IOKit/IOCFPlugIn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "../../tiger/plugin/lbp_usbio.h"

static double now(void) { struct timeval t; gettimeofday(&t, NULL); return t.tv_sec + t.tv_usec / 1e6; }

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	int rounds = argc > 1 ? atoi(argv[1]) : 3, i, bad = 0;
	mach_port_t master = 0; io_iterator_t it = 0; io_service_t svc; IOUSBInterfaceInterface **ifc = NULL; struct Priv *p;
	CFMutableDictionaryRef m; SInt32 cls = 7, sub = 1; CFNumberRef nc, ns; IOReturn k; UInt8 n = 0, pi;
	if (IOMasterPort(MACH_PORT_NULL, &master) != kIOReturnSuccess) return 2;
	m = IOServiceMatching("IOUSBInterface");
	nc = CFNumberCreate(NULL, kCFNumberSInt32Type, &cls); ns = CFNumberCreate(NULL, kCFNumberSInt32Type, &sub);
	CFDictionarySetValue(m, CFSTR("bInterfaceClass"), nc); CFDictionarySetValue(m, CFSTR("bInterfaceSubClass"), ns);
	if (IOServiceGetMatchingServices(master, m, &it) != kIOReturnSuccess) { printf("FAIL: matching\n"); return 1; }
	while ((svc = IOIteratorNext(it)) != 0) {
		IOCFPlugInInterface **plug = NULL; SInt32 score;
		if (IOCreatePlugInInterfaceForService(svc, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &plug, &score) == kIOReturnSuccess && plug) {
			(*plug)->QueryInterface(plug, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID), (LPVOID *)&ifc);
			(*plug)->Release(plug);
		}
		IOObjectRelease(svc);
		if (ifc) break;
	}
	IOObjectRelease(it);
	if (!ifc) { printf("FAIL: printer interface not found\n"); return 1; }
	p = calloc(1, sizeof *p);
	if ((*ifc)->QueryInterface(ifc, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID182), (LPVOID *)&p->i182) != S_OK) { printf("FAIL: no 182 interface\n"); return 1; }
	if ((k = (*ifc)->USBInterfaceOpen(ifc)) != kIOReturnSuccess) { printf("FAIL: USBInterfaceOpen = 0x%x (kIOReturnExclusiveAccess = 0xe00002c5: someone else has it)\n", (unsigned)k); return 1; }
	(*ifc)->GetNumEndpoints(ifc, &n);
	for (pi = 1; pi <= n; pi++) {
		UInt8 dir, num, tt, iv; UInt16 mps;
		if ((*ifc)->GetPipeProperties(ifc, pi, &dir, &num, &tt, &mps, &iv) == kIOReturnSuccess) {
			printf("pipe %d: dir %d ep %d type %d maxpacket %d\n", pi, dir, num, tt, mps);
			if (tt == kUSBBulk) { if (dir == kUSBIn) p->in = pi; else p->out = pi; }
		}
	}
	if (!p->in || !p->out) { printf("FAIL: bulk pipes missing\n"); (*ifc)->USBInterfaceClose(ifc); return 1; }
	{
		struct capt_io io; uint8_t buf[600]; struct capt_status st; struct capt_cartridge cr[4]; int r; double t0;
		io.ctx = p; io.write = io_write; io.read = io_read; io.sleep_ms = io_sleep;
		{   /* raw exchange first: drain whatever is pending on the IN pipe (stale replies from earlier runs), then watch how ONE A0A8 answer arrives (chunk sizes) */
			uint8_t q[4] = { 0xa8, 0xa0, 4, 0 }; int k2, j = 0, total = 0, want = 0, chunks = 0;
			for (j = 0; j < 20; j++) {
				k2 = io_read(p, buf, sizeof buf, 300); if (k2 <= 0) break;
				printf("raw: drained %d stale bytes:", k2); for (j = 0; j < k2 && j < 24; j++) printf(" %02x", buf[j]); printf("\n");
			}
			k2 = io_write(p, q, 4); printf("raw: wrote A0A8 -> %d\n", k2);
			t0 = now();
			while (total < (want ? want : 4) && chunks < 20) {
				k2 = io_read(p, buf + total, sizeof buf - total, 2000); chunks++;
				printf("raw: chunk %d: %d bytes after %.0f ms\n", chunks, k2, (now() - t0) * 1000);
				if (k2 <= 0) break;
				total += k2; if (total >= 4 && !want) want = buf[2] | buf[3] << 8;
			}
			printf("raw: total %d bytes, header cmd=%02x%02x size=%d:", total, total > 1 ? buf[1] : 0, total > 0 ? buf[0] : 0, want); for (j = 0; j < total && j < 40; j++) printf(" %02x", buf[j]); printf("\n");
		}
		for (i = 0; i < rounds; i++) {
			t0 = now(); r = capt_get_status(&io, &st);
			printf("A0A8 status: rc=%d %.0f ms  S0..6=%04x %04x %04x %04x %04x %04x %04x  decoding=%u printing=%u out=%u completed=%u  state=%s cond=%x\n", r, (now() - t0) * 1000,
			       st.st[0], st.st[1], st.st[2], st.st[3], st.st[4], st.st[5], st.st[6], st.page_decoding, st.page_printing, st.page_out, st.page_completed,
			       r == 0 ? capt_state_name(capt_status_summary(&st)) : "-", r == 0 ? capt_status_conditions(&st) : 0);
			if (r) bad++;
			t0 = now(); r = capt_get_cartridges(&io, cr, 4);
			printf("A0A4 cartridges: n=%d %.0f ms\n", r, (now() - t0) * 1000); if (r < 0) bad++;
			t0 = now(); r = capt_xact(&io, PC_GET_DEVICEID, NULL, 0, buf, sizeof buf, 2000);
			printf("A1A0 device id: rc=%d %.0f ms \"%.*s\"\n", r, (now() - t0) * 1000, r > 4 ? r - 4 : 0, r > 4 ? (char *)buf + 4 : ""); if (r < 0) bad++;
		}
		/* informational: ReadPipeTO behaviour on a timeout */
		t0 = now(); r = io_read(p, buf, sizeof buf, 300);
		printf("idle read (300 ms timeout): rc=%d after %.0f ms (informational: real ReadPipeTO timeouts are coarse (~1-2 s) and can swallow the first packet of the NEXT reply, so the engine never reads speculatively)\n", r, (now() - t0) * 1000);
		{   /* what exactly arrives after a timed-out read? */
			uint8_t q[4] = { 0xa8, 0xa0, 4, 0 }; int k2, j, total = 0, ch = 0;
			k2 = io_write(p, q, 4); printf("after timeout: wrote A0A8 -> %d\n", k2);
			while (total < 92 && ch < 8) { k2 = io_read(p, buf + total, sizeof buf - total, 2000); ch++; printf("  chunk %d: %d bytes\n", ch, k2); if (k2 <= 0) break; total += k2; }
			printf("  total %d:", total); for (j = 0; j < total && j < 40; j++) printf(" %02x", buf[j]); printf("\n");
		}
		r = capt_get_status(&io, &st); printf("status after idle read: rc=%d (informational)\n", r);
	}
	(*p->i182)->Release(p->i182);
	(*ifc)->USBInterfaceClose(ifc); (*ifc)->Release(ifc);
	printf("%s\n", bad ? "PROBE: PROBLEMS" : "PROBE: OK");
	return bad ? 1 : 0;
}
