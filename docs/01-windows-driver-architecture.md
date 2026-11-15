# Canon LBP6200d Windows driver (R1.00 V1.10) — architecture notes

Source: LBP6200d_R100_V110_W32_us_EN.exe (x86). Decompiled with Ghidra 12.1.3
(`~/lbp6200/ghidra/out/*.c`, helper `~/lbp6200/tools/fn <dll.c-name> <addr>`).
Function names below are my own; Ghidra addresses are image addresses.

## Component layering (print path)
    app -> GDI -> cnabdm.dll (GDI plug-in; CAPTIMG/UFRIMG data types; uses cnabdrdk.dll UFR renderer)
        -> spooler -> cnap2lmk.dll (language monitor; InitializePrintMonitor[2])
              -> cnabdemk.dll (NewEmonInit; "EMON" engine monitor: job/page/port state machine)
                    -> spooler port (USBPRINT) WritePort / ReadPort callbacks
    status UI: cnabdswk.exe (CNAB*_Status classes), cnap2smk.dll (SmPsw_* RPC to monitor)

* cnabdrdk.dll is Canon's UFR renderer (u:\ufr_sw\...) — not protocol; not needed.
* cnabdemk.dll holds everything protocol-related: classes C_Port, C_Job, C_Page,
  C_InternalJob, C_QP (qp.cpp), plus emonsst.cpp / empage.cpp / internal.cpp.
  "CAPT Interface Version : 3.0".

## EMON request queue (cnabdemk.dll)
Callers (the EMON_* API) enqueue requests via `FUN_10008120(port, reqId, in, inLen,
out, outLen, &got, async)` (CallDeviceRequest). A worker thread executes them
(switch at ~line 29895 of cnabdemk.dll.c). Selected request IDs:
 0x0a  send CAPT command + read reply  -> FUN_10021670 -> FUN_10026b60
 0x13/0x14/0x19/0x25/0x28/0x30/0x3e/0x4b/0x4c/0x4f/0x50/0x57/0x58: device queries/ops
       (version string, device id, cartridge mode, rotate cartridge ...)
A flag at port+0x754 selects a virtual/dummy device (canned replies, e.g. device-id
"123456789ABC", vendor 0x04a9).

## Command transact (FUN_10026b60)
 1. (E1AC..E1B0, A3B8 get a no-op repack — byte copies, no transform)
 2. FUN_100255a0: write the packet through port callback at this+0x10
    (loops until all bytes written; mutex-protected)
 3. FUN_10025e30: read reply (min 6 bytes) ; FUN_10024d00 repacks 6-byte header.
 Reply byte 4 bit 0x80 set => error (checked by the E1Ax senders).

## Command words seen in cnabdemk.dll
 D0A0-D0A9, D0B4-D0B7 (page/compression, parsed by SetCommandAddress ~l.20690)
 E0AE, E1A1-E1A3, E1AC-E1B0 (job/notification; E1AC/E1AF send 0x30-byte packets)
 A1A1, A1AB (fake device-id reply), A3A9, A3AE, A3B8
Layout details: TODO (see 02-command-set.md once written).
