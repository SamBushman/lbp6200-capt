/* USB class-driver plug-in for the Canon LBP6200 on Mac OS X 10.4 Tiger.  Loaded by Apple's own `usb` backend (CFPlugIn type 06047D16-53A2-11D6-9206-003065524592)
 * because a codeless merge-nub kext publishes "USB Printing Class" on the printer's IOUSBInterface - exactly how Xerox ships its printers.
 * The object layout and call protocol below were derived from Apple's USBGenericPrintingClass.plugin and the backend (docs/13 section 2).
 *
 *   outer object  = what the backend holds as `self`:  { block*, factoryID, refcount, private* }   (the first three fields are what Apple's generic class driver has)
 *   block (0x60)  = *self: IUnknown-style function table PLUS data fields the backend and the generic driver share, so the offsets are fixed:
 *                   +28 IOUSBInterfaceInterface** (the backend writes it), +2c/+2d bulk-out/in pipeRefs, +30.. the class-driver methods.
 * The bytes of a job (LBPC records from rastertolbpc) arrive in WritePipe(); they are replayed into the CAPT engine (lbpc/lbpc_drive.c) which talks to the printer
 * through the interface's bulk pipes.  ReadPipe() always reports "no data": the replies belong to the engine (Tiger's CUPS has no back-channel anyway).  GPL-3. */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../../lbpc/lbpc.h"

#define IFACE_UUID  CFUUIDGetConstantUUIDWithBytes(NULL, 0x03, 0x34, 0x6d, 0x74, 0x53, 0xa3, 0x11, 0xd6, 0x9e, 0xa1, 0x76, 0x30, 0x65, 0x52, 0x45, 0x92)
#define IUNKNOWN_UUID CFUUIDGetConstantUUIDWithBytes(NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46)

struct Obj;
typedef struct Block {
	void *reserved;                                                           /* +00 */
	HRESULT (*QueryInterface)(struct Obj *self, REFIID iid, LPVOID *ppv);     /* +04 */
	ULONG (*AddRef)(struct Obj *self);                                        /* +08 */
	ULONG (*Release)(struct Obj *self);                                       /* +0c */
	CFStringRef cachedID;                                                     /* +10 cached 1284 device ID (released by +5c) */
	UInt32 pad14, pad18;                                                      /* +14 +18 */
	UInt32 locationID;                                                        /* +1c */
	UInt8 ifNumber, pad21, altSetting, pad23, ifProtocol, pad25[3];           /* +20 +22 +24 */
	IOUSBInterfaceInterface **iface;                                          /* +28 injected by the backend */
	UInt8 outPipe, inPipe, pad2e, pad2f;                                      /* +2c +2d */
	IOReturn (*Control)(struct Obj *self, IOUSBDevRequest *req, UInt32 timeout);               /* +30 */
	IOReturn (*GetString)(struct Obj *self, UInt8 index, UInt16 langid, UInt32 timeout, CFStringRef *out);   /* +34 */
	IOReturn (*SoftReset)(struct Obj *self, UInt32 timeout);                  /* +38 */
	IOReturn (*GetPortStatus)(struct Obj *self, UInt8 *status);               /* +3c */
	IOReturn (*GetDeviceID)(struct Obj *self, CFStringRef *out, UInt32 timeout);               /* +40 */
	IOReturn (*ReadPipe)(struct Obj *self, UInt8 *buf, UInt32 *len);         /* +44 */
	IOReturn (*WritePipe)(struct Obj *self, UInt8 *buf, UInt32 *len, UInt32 flags);          /* +48 */
	IOReturn (*Open)(struct Obj *self, UInt32 locationID, UInt32 protocol);   /* +4c */
	IOReturn (*Abort)(struct Obj *self);                                      /* +50 */
	IOReturn (*Close)(struct Obj *self);                                      /* +54 */
	IOReturn (*Init)(struct Obj *self, void *unused);                         /* +58 */
	IOReturn (*ReleaseID)(struct Obj *self);                                  /* +5c */
} Block;

/* the offsets the backend relies on must not drift */
typedef char chk_size[sizeof(Block) == 0x60 ? 1 : -1];
typedef char chk_iface[__builtin_offsetof(Block, iface) == 0x28 ? 1 : -1];
typedef char chk_pipes[__builtin_offsetof(Block, outPipe) == 0x2c && __builtin_offsetof(Block, inPipe) == 0x2d ? 1 : -1];
typedef char chk_ctl[__builtin_offsetof(Block, Control) == 0x30 && __builtin_offsetof(Block, ReleaseID) == 0x5c && __builtin_offsetof(Block, Open) == 0x4c ? 1 : -1];

struct Priv;
typedef struct Obj { Block *blk; CFUUIDRef factoryID; UInt32 refcount; struct Priv *p; } Obj;

#include "lbp_usbio.h"
static void drive_log(void *c, const char *line) { (void)c; fprintf(stderr, "%s\n", line); fflush(stderr); }

/* ---------- COM-ish plumbing ---------- */
static HRESULT qi(Obj *self, REFIID iid, LPVOID *ppv)
{
	CFUUIDRef u = CFUUIDCreateFromUUIDBytes(NULL, iid);
	int ok;
	if (!u) { *ppv = NULL; return E_NOINTERFACE; }
	ok = CFEqual(u, IFACE_UUID) || CFEqual(u, IUNKNOWN_UUID);
	CFRelease(u);
	if (!ok) { *ppv = NULL; return E_NOINTERFACE; }
	self->blk->AddRef(self);
	*ppv = self;
	return S_OK;
}
static ULONG addref(Obj *self) { return ++self->refcount; }
static void obj_free(Obj *self);
static ULONG release(Obj *self)
{
	ULONG r = --self->refcount;
	if (r == 0) obj_free(self);
	return r;
}

static IOReturn m_close(Obj *self);
static void obj_free(Obj *self)
{
	CFUUIDRef f = self->factoryID;
	/* The backend owns the IOUSBInterfaceInterface it wrote into +28 and releases it itself (discovery: Load, Init, inject, GetDeviceID, Unload, then its own Release;
	 * printing: Close() releases it).  Releasing it here was a double release (found by makequeues crashing) - only drop what WE took. */
	if (self->p) { if (self->p->i182) (*self->p->i182)->Release(self->p->i182); free(self->p); self->p = NULL; }
	free(self->blk); self->blk = NULL;
	free(self);
	if (f) { CFPlugInRemoveInstanceForFactory(f); CFRelease(f); }
}

/* ---------- control requests (class requests of the USB printer class) ---------- */
/* The interface the backend injected, queried for the 182 table (our own reference, dropped in Close()/free).  Works before Open(): the backend's discovery path
 * (usb backend without arguments, makequeues) only does Load, Init, inject, GetDeviceID, Unload. */
static IOUSBInterfaceInterface182 **get182(Obj *self)
{
	IOUSBInterfaceInterface **ifc = self->blk->iface;
	if (!ifc) return NULL;
	if (!self->p) { self->p = calloc(1, sizeof *self->p); if (!self->p) return NULL; }
	if (!self->p->i182) {
		IOUSBInterfaceInterface182 **q = NULL;
		if ((*ifc)->QueryInterface(ifc, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID182), (LPVOID *)&q) != S_OK) return NULL;
		self->p->i182 = q;
	}
	return self->p->i182;
}
/* the generic driver asks the interface every time: interface number (block +20), alternate setting, configuration */
static void refresh_ids(Obj *self, UInt8 *config)
{
	IOUSBInterfaceInterface **ifc = self->blk->iface; UInt8 v;
	if (!ifc) return;
	if ((*ifc)->GetInterfaceNumber(ifc, &v) == kIOReturnSuccess) self->blk->ifNumber = v;
	if ((*ifc)->GetAlternateSetting(ifc, &v) == kIOReturnSuccess) self->blk->altSetting = v;
	if (config && (*ifc)->GetConfigurationValue(ifc, &v) == kIOReturnSuccess) *config = v;
}
static IOReturn ctl(Obj *self, IOUSBDevRequest *req, UInt32 timeout)
{
	IOUSBInterfaceInterface182 **i = get182(self);
	IOUSBDevRequestTO r;
	if (!i) return kIOReturnNotOpen;
	r.bmRequestType = req->bmRequestType; r.bRequest = req->bRequest; r.wValue = req->wValue; r.wIndex = req->wIndex; r.wLength = req->wLength;
	r.pData = req->pData; r.wLenDone = 0;
	r.noDataTimeout = r.completionTimeout = (timeout && timeout <= 60000) ? timeout : 5000;   /* the backend passes garbage to some calls */
	{ IOReturn k = (*i)->ControlRequestTO(i, 0, &r); req->wLenDone = r.wLenDone; return k; }
}
static IOReturn m_control(Obj *self, IOUSBDevRequest *req, UInt32 timeout) { return ctl(self, req, timeout); }

static IOReturn m_getstring(Obj *self, UInt8 index, UInt16 langid, UInt32 timeout, CFStringRef *out)
{
	UInt8 d[256]; IOUSBDevRequest r; IOReturn k; UniChar u[127]; CFIndex n, j;
	*out = NULL;
	if (!index) return kIOReturnBadArgument;
	r.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBStandard, kUSBDevice); r.bRequest = kUSBRqGetDescriptor;
	r.wValue = (UInt16)((kUSBStringDesc << 8) | index); r.wIndex = langid ? langid : 0x0409; r.wLength = sizeof d; r.pData = d; r.wLenDone = 0;
	if ((k = ctl(self, &r, timeout)) != kIOReturnSuccess) return k;
	if (r.wLenDone < 2) return kIOReturnUnderrun;
	n = (d[0] < r.wLenDone ? d[0] : r.wLenDone); n = (n - 2) / 2;
	for (j = 0; j < n && j < 127; j++) u[j] = (UniChar)(d[2 + 2 * j] | d[3 + 2 * j] << 8);
	*out = CFStringCreateWithCharacters(NULL, u, j);
	return *out ? kIOReturnSuccess : kIOReturnNoMemory;
}
static IOReturn m_softreset(Obj *self, UInt32 timeout)
{
	IOUSBDevRequest r; memset(&r, 0, sizeof r); refresh_ids(self, NULL);
	r.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBClass, kUSBInterface); r.bRequest = 2; r.wIndex = self->blk->ifNumber;
	return ctl(self, &r, timeout);
}
static IOReturn m_portstatus(Obj *self, UInt8 *status)
{
	IOUSBDevRequest r; memset(&r, 0, sizeof r); *status = 0; refresh_ids(self, NULL);
	r.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBClass, kUSBInterface); r.bRequest = 1; r.wIndex = self->blk->ifNumber; r.wLength = 1; r.pData = status;
	return ctl(self, &r, 0);
}
/* GET_DEVICE_ID (printer class request 0): 2-byte length (big-endian per spec; some devices send little-endian) + IEEE 1284 string; wIndex = interface<<8 | alternate */
static IOReturn m_getdeviceid(Obj *self, CFStringRef *out, UInt32 timeout)
{
	UInt8 d[1024]; IOUSBDevRequest r; IOReturn k; unsigned len; UInt8 cfg = 1;
	*out = NULL;
	memset(&r, 0, sizeof r); memset(d, 0, sizeof d);
	refresh_ids(self, &cfg);
	r.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBClass, kUSBInterface); r.bRequest = 0; r.wValue = (UInt16)(cfg ? cfg - 1 : 0);
	r.wIndex = (UInt16)((self->blk->ifNumber << 8) | self->blk->altSetting); r.wLength = sizeof d - 1; r.pData = d;
	if ((k = ctl(self, &r, timeout)) != kIOReturnSuccess) return k;
	if (r.wLenDone < 2) return kIOReturnUnderrun;
	len = (unsigned)d[0] << 8 | d[1];
	if (len > r.wLenDone) len = (unsigned)d[1] << 8 | d[0];            /* little-endian devices */
	if (len > r.wLenDone) len = r.wLenDone;
	if (len < 2) return kIOReturnUnderrun;
	*out = CFStringCreateWithBytes(NULL, d + 2, len - 2, kCFStringEncodingISOLatin1, false);
	return *out ? kIOReturnSuccess : kIOReturnNoMemory;
}

/* ---------- data path ---------- */
/* Apple's backend polls this on a second thread every 0.25 s and forwards any data to ITS stdout; our replies stay inside the engine, so: never any data */
static IOReturn m_readpipe(Obj *self, UInt8 *buf, UInt32 *len) { (void)self; (void)buf; *len = 0; return kIOReturnSuccess; }

/* the backend passes the job stream here in 2048-byte pieces and, on failure, prints the error, calls Abort() and Close() */
static IOReturn m_writepipe(Obj *self, UInt8 *buf, UInt32 *len, UInt32 flags)
{
	int e;
	(void)flags;
	if (!self->p || !self->p->drive_ready) return kIOReturnNotOpen;
	e = lbpc_drive_feed(&self->p->drive, buf, *len);
	if (e) { fprintf(stderr, "ERROR: LBP6200 job failed (%d)\n", e); fflush(stderr); return e == CAPT_ECANCEL ? kIOReturnAborted : kIOReturnIOError; }
	return kIOReturnSuccess;
}
static IOReturn m_abort(Obj *self)
{
	if (self->p && self->p->drive_ready) lbpc_drive_cancel(&self->p->drive);
	return kIOReturnSuccess;
}

/* ---------- open / close ---------- */
static IOReturn find_interface(Obj *self, UInt32 locationID);
static IOReturn m_open(Obj *self, UInt32 locationID, UInt32 protocol)
{
	Block *b = self->blk; struct Priv *p; IOUSBInterfaceInterface **ifc; IOReturn k; UInt8 n = 0, i;
	(void)protocol;     /* our device has the one alternate setting with protocol 2 (bidirectional); the backend tries 2 then 1 */
	if (!self->p) { self->p = calloc(1, sizeof *self->p); if (!self->p) return kIOReturnNoMemory; }
	p = self->p;
	if (!b->iface && (k = find_interface(self, locationID)) != kIOReturnSuccess) return k;
	ifc = b->iface;
	if (!get182(self)) return kIOReturnUnsupported;
	if ((k = (*ifc)->USBInterfaceOpen(ifc)) != kIOReturnSuccess) return k;      /* kIOReturnExclusiveAccess: someone else owns it; the backend retries */
	b->locationID = locationID;
	(*ifc)->GetInterfaceNumber(ifc, &b->ifNumber);
	(*ifc)->GetAlternateSetting(ifc, &b->altSetting);
	(*ifc)->GetInterfaceProtocol(ifc, &b->ifProtocol);
	b->outPipe = b->inPipe = 0;
	if ((k = (*ifc)->GetNumEndpoints(ifc, &n)) != kIOReturnSuccess) goto fail;
	for (i = 1; i <= n; i++) {
		UInt8 dir, num, tt, intv; UInt16 mps;
		if ((*ifc)->GetPipeProperties(ifc, i, &dir, &num, &tt, &mps, &intv) != kIOReturnSuccess) continue;
		if (tt != kUSBBulk) continue;
		if (dir == kUSBIn) b->inPipe = i; else if (!b->outPipe) b->outPipe = i;
	}
	if (!b->outPipe || !b->inPipe) { k = kIOReturnNotFound; goto fail; }        /* the CAPT conversation needs both bulk pipes */
	p->out = b->outPipe; p->in = b->inPipe; p->rxn = p->rxo = 0;
	p->io.ctx = p; p->io.write = io_write; p->io.read = io_read; p->io.sleep_ms = io_sleep;
	lbpc_drive_init(&p->drive, &p->io, drive_log, NULL);
	p->drive_ready = 1;
	return kIOReturnSuccess;
fail:
	(*ifc)->USBInterfaceClose(ifc);
	return k;
}

static IOReturn m_close(Obj *self)
{
	Block *b = self->blk; struct Priv *p = self->p; IOReturn k = kIOReturnSuccess;
	if (p && p->drive_ready) {            /* end of the job stream: wait until the printer has taken every page, or abort what is incomplete */
		int e = lbpc_drive_end(&p->drive);
		if (e) fprintf(stderr, "ERROR: LBP6200 job ended with error %d\n", e);
		fflush(stderr);
		p->drive_ready = 0;
	}
	if (b && b->iface) {
		IOUSBInterfaceInterface **ifc = b->iface;
		if (p && p->i182) { (*p->i182)->Release(p->i182); p->i182 = NULL; }
		k = (*ifc)->USBInterfaceClose(ifc);
		(*ifc)->Release(ifc);
		b->iface = NULL;
	}
	if (b) b->outPipe = b->inPipe = 0;
	return k;
}

/* ---------- finding the interface when the backend did not inject one (same search as the generic driver: printer-class interfaces by location) ---------- */
static IOReturn find_interface(Obj *self, UInt32 locationID)
{
	mach_port_t master = 0; io_iterator_t it = 0; io_service_t svc; IOReturn k;
	CFMutableDictionaryRef m;
	SInt32 cls = 7, sub = 1; CFNumberRef nc, ns;
	if ((k = IOMasterPort(MACH_PORT_NULL, &master)) != kIOReturnSuccess) return k;
	if (!(m = IOServiceMatching("IOUSBInterface"))) return kIOReturnNoMemory;
	nc = CFNumberCreate(NULL, kCFNumberSInt32Type, &cls); ns = CFNumberCreate(NULL, kCFNumberSInt32Type, &sub);
	CFDictionarySetValue(m, CFSTR("bInterfaceClass"), nc); CFDictionarySetValue(m, CFSTR("bInterfaceSubClass"), ns);
	CFRelease(nc); CFRelease(ns);
	if ((k = IOServiceGetMatchingServices(master, m, &it)) != kIOReturnSuccess) return k;
	k = kIOReturnNoDevice;
	while ((svc = IOIteratorNext(it)) != 0) {
		IOCFPlugInInterface **plug = NULL; SInt32 score; IOUSBInterfaceInterface **ifc = NULL;
		if (IOCreatePlugInInterfaceForService(svc, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &plug, &score) == kIOReturnSuccess && plug) {
			(*plug)->QueryInterface(plug, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID), (LPVOID *)&ifc);
			(*plug)->Release(plug);
			if (ifc) {
				UInt32 loc = 0; UInt8 pr = 0;
				if ((*ifc)->GetLocationID(ifc, &loc) == kIOReturnSuccess && loc == locationID && (*ifc)->GetInterfaceProtocol(ifc, &pr) == kIOReturnSuccess) {
					self->blk->iface = ifc; k = kIOReturnSuccess; IOObjectRelease(svc); break;
				}
				(*ifc)->Release(ifc);
			}
		}
		IOObjectRelease(svc);
	}
	IOObjectRelease(it);
	return k;
}

/* +58: (re)initialise the block, as Apple's generic driver does right after the backend instantiated it */
static IOReturn m_init(Obj *self, void *unused)
{
	Block *b = self->blk; (void)unused;
	b->outPipe = b->inPipe = 0; b->iface = NULL; b->pad18 = 0;
	b->AddRef = addref; b->Release = release; b->QueryInterface = qi;
	b->Control = m_control; b->GetString = m_getstring; b->SoftReset = m_softreset; b->GetPortStatus = m_portstatus; b->GetDeviceID = m_getdeviceid;
	b->ReadPipe = m_readpipe; b->WritePipe = m_writepipe; b->Open = m_open; b->Abort = m_abort; b->Close = m_close;
	b->Init = m_init;
	return kIOReturnSuccess;
}
static IOReturn m_releaseid(Obj *self)
{
	if (self->blk->cachedID) CFRelease(self->blk->cachedID);
	self->blk->cachedID = NULL;
	return kIOReturnSuccess;
}

static Obj *obj_create(CFUUIDRef factoryID)
{
	Obj *o = calloc(1, sizeof *o);
	if (!o) return NULL;
	o->blk = calloc(1, sizeof(Block));
	if (!o->blk) { free(o); return NULL; }
	o->blk->ReleaseID = m_releaseid;
	m_init(o, NULL);
	if (factoryID) { o->factoryID = (CFUUIDRef)CFRetain(factoryID); CFPlugInAddInstanceForFactory(factoryID); }
	o->refcount = 1;
	return o;
}

void *USBPrinterClassFactory(CFAllocatorRef allocator, CFUUIDRef typeID)
{
	(void)allocator;
	if (CFEqual(typeID, CFUUIDGetConstantUUIDWithBytes(NULL, 0x06, 0x04, 0x7d, 0x16, 0x53, 0xa2, 0x11, 0xd6, 0x92, 0x06, 0x00, 0x30, 0x65, 0x52, 0x45, 0x92))) {
		CFUUIDRef f = CFUUIDCreateFromString(NULL, CFSTR("F6DF49CB-1DE2-4F50-879D-A498429CFAF4"));
		Obj *o = obj_create(f);
		if (f) CFRelease(f);
		return o;
	}
	return NULL;
}
