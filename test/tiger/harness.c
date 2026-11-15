/* Mac OS X 10.4 test harness for the LBP6200 class-driver plug-in: NO printer, NO USB, NO kernel code.
 * It does what Apple's usb backend does - load the bundle as a CFPlugIn of type 06047D16-..., QueryInterface the class-driver interface, write the
 * IOUSBInterfaceInterface at block+0x28, Open(location, 2), pass the job in 2048-byte WritePipe() pieces while a second thread polls ReadPipe(), Close() -
 * but the IOUSBInterfaceInterface is a MOCK backed by test/lbpc/mockprinter.h.  Every frame the plug-in sends is written to WIRE for comparison with the
 * Linux golden capture (compare_wire.py).
 * usage: harness BUNDLE STREAM WIRE [fail-after-bytes] */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../../lbpc/lbpc.h"
#include "../lbpc/mockprinter.h"

static int checks, bad;
#define CHECK(c, ...) do { checks++; if (!(c)) { bad++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---------- mock IOUSBInterfaceInterface182 ---------- */
static int mock_open_count, mock_closed, mock_released, mock_refs = 1;
static int mock_inject_fail_after = -1; static long written_total;
static IOUSBInterfaceInterface182 mock_tab;
static IOUSBInterfaceInterface182 *mock_ptr = &mock_tab;          /* an interface "object" is a pointer to the table */
static IOUSBInterfaceInterface182 **mock_obj = &mock_ptr;

static HRESULT mk_qi(void *s, REFIID iid, LPVOID *ppv)
{
	CFUUIDRef u = CFUUIDCreateFromUUIDBytes(NULL, iid); int ok = CFEqual(u, kIOUSBInterfaceInterfaceID182) || CFEqual(u, kIOUSBInterfaceInterfaceID) || CFEqual(u, IUnknownUUID);
	CFRelease(u);
	if (!ok) { *ppv = NULL; return E_NOINTERFACE; }
	mock_refs++; *ppv = s; return S_OK;
}
static ULONG mk_addref(void *s) { return ++mock_refs; }
static ULONG mk_release(void *s) { mock_released++; return --mock_refs; }
static IOReturn mk_open(void *s) { mock_open_count++; return kIOReturnSuccess; }
static IOReturn mk_close(void *s) { mock_closed++; return kIOReturnSuccess; }
static IOReturn mk_u8(void *s, UInt8 *v) { *v = 0; return kIOReturnSuccess; }
static IOReturn mk_proto(void *s, UInt8 *v) { *v = 2; return kIOReturnSuccess; }
static IOReturn mk_nep(void *s, UInt8 *v) { *v = 2; return kIOReturnSuccess; }
static IOReturn mk_pipeprops(void *s, UInt8 pipe, UInt8 *dir, UInt8 *num, UInt8 *tt, UInt16 *mps, UInt8 *iv)
{   /* pipe 1 = bulk OUT ep 2, pipe 2 = bulk IN ep 1 (the order of the real printer is not assumed by the plug-in) */
	*dir = pipe == 1 ? kUSBOut : kUSBIn; *num = pipe == 1 ? 2 : 1; *tt = kUSBBulk; *mps = 512; *iv = 0; return kIOReturnSuccess;
}
static IOReturn mk_write(void *s, UInt8 pipe, void *buf, UInt32 n)
{
	if (pipe != 1) return kIOReturnBadArgument;
	written_total += n;
	if (mock_inject_fail_after >= 0 && written_total > mock_inject_fail_after) return kIOReturnNoDevice;
	return mock_write(NULL, buf, n) ? kIOReturnIOError : kIOReturnSuccess;
}
static IOReturn mk_readto(void *s, UInt8 pipe, void *buf, UInt32 *size, UInt32 t1, UInt32 t2)
{
	int k;
	if (pipe != 2) return kIOReturnBadArgument;
	k = mock_read(NULL, buf, *size, 0);       /* a real bulk-in transfer returns the whole reply message in one go */
	if (k <= 0) { *size = 0; return kIOUSBTransactionTimeout; }
	*size = (UInt32)k; return kIOReturnSuccess;
}
static IOReturn mk_ctl(void *s, UInt8 pipe, IOUSBDevRequestTO *r)
{   /* GET_DEVICE_ID (class request 0): big-endian length + string */
	static const char id[] = "MFG:Canon;CMD:CAPT;MDL:LBP6200;CLS:PRINTER;";
	if ((r->bmRequestType & 0x60) == 0x20 && r->bRequest == 0) {
		unsigned n = (unsigned)strlen(id) + 2; ((UInt8 *)r->pData)[0] = (UInt8)(n >> 8); ((UInt8 *)r->pData)[1] = (UInt8)n;
		memcpy((UInt8 *)r->pData + 2, id, strlen(id)); r->wLenDone = n;
		checks++; if (r->wIndex != 0x0000) { bad++; printf("FAIL: device-id wIndex %04x\n", r->wIndex); }
		return kIOReturnSuccess;
	}
	return kIOReturnUnsupported;
}
static void mock_init(void)
{
	memset(&mock_tab, 0, sizeof mock_tab);
	mock_tab.QueryInterface = mk_qi; mock_tab.AddRef = mk_addref; mock_tab.Release = mk_release;
	mock_tab.USBInterfaceOpen = mk_open; mock_tab.USBInterfaceClose = mk_close;
	mock_tab.GetInterfaceNumber = mk_u8; mock_tab.GetAlternateSetting = mk_u8; mock_tab.GetInterfaceProtocol = mk_proto;
	mock_tab.GetNumEndpoints = mk_nep; mock_tab.GetPipeProperties = mk_pipeprops;
	mock_tab.WritePipe = mk_write; mock_tab.ReadPipeTO = mk_readto; mock_tab.ControlRequestTO = mk_ctl;
	memset(&M, 0, sizeof M); M.asleep = 1;
}

/* ---------- what the backend does ---------- */
typedef struct { void **blk; } Self;           /* self -> block pointer; the block is the function table + shared data */
#define SLOT(s, off) (*(void **)((char *)(*(void ***)(s)) + (off)))
static volatile int reader_stop; static long reader_polls, reader_data;
static void *reader(void *self)
{
	typedef int (*rd_t)(void *, UInt8 *, UInt32 *);
	UInt8 buf[0x1ff];
	while (!reader_stop) {
		UInt32 len = 0x1ff; int r = ((rd_t)SLOT(self, 0x44))(self, buf, &len);
		reader_polls++; if (r == 0 && len) reader_data += len;
		usleep(20000);
	}
	return NULL;
}

int main(int argc, char **argv)
{
	CFURLRef url; CFPlugInRef plug; CFArrayRef facs; void *inst = NULL, *self = NULL; FILE *fs; uint8_t *stream; long n, off = 0; int rc;
	typedef int (*qi_t)(void *, CFUUIDBytes, void **); typedef int (*rel_t)(void *); typedef int (*init_t)(void *, void *);
	typedef int (*open_t)(void *, UInt32, UInt32); typedef int (*wr_t)(void *, UInt8 *, UInt32 *, UInt32); typedef int (*cl_t)(void *);
	typedef int (*id_t)(void *, CFStringRef *, UInt32); typedef int (*ab_t)(void *);
	CFUUIDRef type = CFUUIDGetConstantUUIDWithBytes(NULL, 0x06, 0x04, 0x7d, 0x16, 0x53, 0xa2, 0x11, 0xd6, 0x92, 0x06, 0x00, 0x30, 0x65, 0x52, 0x45, 0x92);
	CFUUIDRef iface = CFUUIDGetConstantUUIDWithBytes(NULL, 0x03, 0x34, 0x6d, 0x74, 0x53, 0xa3, 0x11, 0xd6, 0x9e, 0xa1, 0x76, 0x30, 0x65, 0x52, 0x45, 0x92);
	pthread_t th; int fail_mode;
	if (argc < 4) { fprintf(stderr, "usage: harness BUNDLE STREAM WIRE [fail-after-bytes]\n"); return 2; }
	fail_mode = argc > 4; mock_init();
	if (fail_mode) mock_inject_fail_after = atoi(argv[4]);
	mock_wire = fopen(argv[3], "wb"); if (!mock_wire) { perror("wire"); return 2; }
	fs = fopen(argv[2], "rb"); if (!fs) { perror("stream"); return 2; }
	fseek(fs, 0, SEEK_END); n = ftell(fs); fseek(fs, 0, SEEK_SET); stream = malloc(n); if (fread(stream, 1, n, fs) != (size_t)n) return 2; fclose(fs);

	url = CFURLCreateWithFileSystemPath(NULL, CFStringCreateWithCString(NULL, argv[1], kCFStringEncodingUTF8), kCFURLPOSIXPathStyle, true);
	plug = CFPlugInCreate(NULL, url); CHECK(plug != NULL, "CFPlugInCreate failed for %s", argv[1]);
	if (!plug) return 1;
	facs = CFPlugInFindFactoriesForPlugInTypeInPlugIn(type, plug); CHECK(facs && CFArrayGetCount(facs) == 1, "expected exactly one factory for the printing-class type");
	if (!facs || !CFArrayGetCount(facs)) return 1;
	inst = CFPlugInInstanceCreate(NULL, CFArrayGetValueAtIndex(facs, 0), type); CHECK(inst != NULL, "factory returned NULL");
	if (!inst) return 1;
	{ UInt32 hr = ((qi_t)SLOT(inst, 0x04))(inst, CFUUIDGetUUIDBytes(iface), &self); CHECK(hr == S_OK && self != NULL, "QueryInterface(class-driver) = %lx", (long)hr); }
	{ void *dummy = NULL; UInt32 hr = ((qi_t)SLOT(inst, 0x04))(inst, CFUUIDGetUUIDBytes(CFUUIDGetConstantUUIDWithBytes(NULL, 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16)), &dummy); CHECK(hr != S_OK && dummy == NULL, "unknown IID must be refused"); }
	((rel_t)SLOT(inst, 0x0c))(inst);
	((init_t)SLOT(self, 0x58))(self, NULL);                               /* backend: Init(NULL) right after loading */
	*(void **)((char *)(*(void ***)self) + 0x28) = mock_obj;              /* backend: write the IOUSBInterfaceInterface** into the block */
	rc = ((open_t)SLOT(self, 0x4c))(self, 0x12345678, 2); CHECK(rc == 0, "Open = %x", rc);
	CHECK(*((UInt8 *)(*(void ***)self) + 0x2c) == 1 && *((UInt8 *)(*(void ***)self) + 0x2d) == 2, "pipeRefs out/in = %d/%d (want 1/2)", *((UInt8 *)(*(void ***)self) + 0x2c), *((UInt8 *)(*(void ***)self) + 0x2d));
	{ CFStringRef idstr = NULL; int r = ((id_t)SLOT(self, 0x40))(self, &idstr, 5000); char b[200] = "";
	  CHECK(r == 0 && idstr, "GetDeviceID = %x", r);
	  if (idstr) { CFStringGetCString(idstr, b, sizeof b, kCFStringEncodingASCII); CHECK(!strcmp(b, "MFG:Canon;CMD:CAPT;MDL:LBP6200;CLS:PRINTER;"), "device id '%s'", b); CFRelease(idstr); } }
	pthread_create(&th, NULL, reader, self);
	while (off < n) {
		UInt32 len = (UInt32)(n - off < 2048 ? n - off : 2048);
		int r = ((wr_t)SLOT(self, 0x48))(self, stream + off, &len, 0);
		if (r) { printf("WritePipe failed (%x) at byte %ld -> Abort, Close (as the backend does)\n", r, off); ((ab_t)SLOT(self, 0x50))(self); break; }
		CHECK(len == (n - off < 2048 ? n - off : 2048), "WritePipe must not change the length (%u)", (unsigned)len);
		off += len;
	}
	rc = ((cl_t)SLOT(self, 0x54))(self);
	reader_stop = 1; pthread_join(th, NULL);
	CHECK(reader_data == 0, "ReadPipe must never return data (%ld bytes)", reader_data);
	CHECK(mock_open_count == 1 && mock_closed == 1, "interface open/close = %d/%d", mock_open_count, mock_closed);
	((cl_t)SLOT(self, 0x5c))(self);                                       /* release cached ID */
	((rel_t)SLOT(self, 0x0c))(self);                                      /* UsbUnloadClassDriver */
	CHECK(mock_refs == 0, "interface refs=%d, want 0: Close() releases the reference the backend handed over (as Apple's generic driver does) and the QueryInterface for the TO calls", mock_refs);
	fclose(mock_wire);
	printf("%s: %d checks, %d failed; %lu frames to the mock printer, ReadPipe polled %ld times\n", fail_mode ? "fail-mode" : "normal", checks, bad, M.frames, reader_polls);
	return bad ? 1 : 0;
}
