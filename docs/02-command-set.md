# CAPT 3.0 printer-level command set (LBP6200d)

Source: `cmdCode2str` in Canon's Linux libcncaptnpm.so.2.0.1 (names are Canon's own).
Extracted with tools/cmdnames.py. The parser misses a few entries (e.g. A0A4
PC_GET_CARTRIDGE_STATUS, D0A5/D0A7/C0A6); see the source function for the rest.
Framing (confirmed on hardware, see evidence/probe-001-status.txt):
  packet = u16le command, u16le total_size (incl. 4-byte header), payload...
  reply  = same layout; command echoed; first payload u16 is a status word
           (0x0000 = ok on the info queries; see "Open questions").
Only image commands (mask 0xC080: C0xx/D0xx) and A2A0 RESERVE_UNIT need the
printer reserved (libcncaptnpm capt_cmd_need_printer_reserved).

## Names
    0xA0A0  PC_GET_EXTENDED_STATUS
    0xA0A1  PC_GET_INPUT_STATUS
    0xA0A2  PC_GET_OUTPUT_STATUS
    0xA0A3  PC_GET_MIB_STATUS
    0xA0A5  PC_GET_TOTAL_COUNTER
    0xA0A6  PC_GET_CALIBRATION_DATA
    0xA0A7  PC_GET_JOB_INFO2
    0xA0A8  PC_GET_EXTENDED_STATUS2
    0xA0A9  PC_GET_DIGREG_DATA
    0xA1A0  PC_GET_DEVICEID
    0xA1A1  PC_GET_PRINTER_INFO
    0xA1A2  PC_GET_INPUT_NAME
    0xA1A3  PC_GET_OUTPUT_NAME
    0xA1A4  PC_GET_CARTRIDGE_NAME
    0xA1A5  PC_GET_COVER_NAME
    0xA1A7  PC_GET_PRINTER_BITMAP
    0xA1A8  PC_GET_ENGINE_VERSION
    0xA1A9  PC_GET_PARTS_LIST
    0xA1AA  PC_GET_ENGINE_ID
    0xA1AB  PC_GET_USB_ID
    0xA2A0  PC_RESERVE_UNIT
    0xA2A1  PC_GET_MISPRINT_STATUS
    0xA2A2  PC_GET_SERVICECALL_STATUS
    0xA2A3  PC_GET_JAM_STATUS
    0xA2A4  PC_GET_WAITING_STATUS
    0xA2A7  PC_GET_ILLEGALCONNECTION_STATUS
    0xA3A0  PC_GET_JOB_INFO
    0xA3A1  PC_GET_FLASH_DATA2
    0xA3A2  PC_ANNOUNCE_JOB
    0xA3A4  PC_GET_CONFIG_PAGE
    0xA3A5  PC_GET_NIC_REQUEST
    0xA3A6  PC_RESPOND_NIC_REQUEST
    0xA3A7  PC_GET_PWM_DATA
    0xA3A8  PC_GET_PARTS_COUNTER
    0xA3A9  PC_SET_TIME
    0xA3AA  PC_GET_ALARM_STATUS
    0xA3AB  PC_GET_PAGE_STATUS
    0xA3AC  PC_GET_SENSOR_VALUE
    0xA3AD  PC_GET_NIC_PARAMETER
    0xA3AE  PC_SET_NIC_PARAMETER
    0xA4A0  PC_DBG_GET_INFO
    0xA4A1  PC_DBG_SET_MIBSTATUS
    0xA4A2  PC_DBG_SET_SERVICECALL_STATUS
    0xA4A3  PC_DBG_SET_JAM_STATUS
    0xA4A5  PC_DBG_SET_CARTRIDGE_STATUS
    0xA4A6  PC_DBG_SET_PARTS_COUNTER
    0xC0A0  IC_VIDEO_DATA
    0xC0A4  IC_BLACK_END
    0xC0A5  IC_CYAN_END
    0xC0A7  IC_YELLOW_END
    0xD0A0  IC_BEGIN_PAGE
    0xD0A1  IC_BEGIN_DATA
    0xD0A2  IC_END_PAGE
    0xD0A4  IC_BLACK_PLANE
    0xD0A6  IC_MAGENTA_PLANE
    0xD0A8  IC_VIDEO_COUNT
    0xD0A9  IC_COMPOUND_DATA
    0xE0A0  PCR_GET_BASIC_STATUS
    0xE0A1  PCR_RESET_ENGINE
    0xE0A3  PCR_CLEAR_MISPRINT
    0xE0A4  PCR_DISCARD_DATA
    0xE0A5  PCR_GO_ONLINE
    0xE0A6  PCR_GO_OFFLINE
    0xE0A7  PCR_START_PRINT
    0xE0A8  PCR_RESET_PRINTER
    0xE0A9  PCR_RELEASE_UNIT
    0xE0AA  PCR_TEST_PRINT
    0xE0AB  PCR_ENGINE_COMMAND
    0xE0AC  PCR_CHECK_RAM
    0xE0AE  PCR_SET_FLASH_DATA
    0xE0B1  PCR_START_CALIBRATION_DMAX
    0xE0B2  PCR_START_CALIBRATION_DHALF
    0xE0B3  PCR_START_PWM_TEST
    0xE0B5  PCR_SET_HELP_MODE
    0xE0B9  PCR_SET_CARTRIDGE_DATE
    0xE0BA  PCR_SET_POWER_DOWN
    0xE1A0  PCR_SET_JOB_INFO
    0xE1A2  PCR_SET_LED_STATUS
    0xE1A3  PCR_SET_NIC_REQUEST
    0xE1A4  PCR_CHANGE_PARTS
    0xE1A5  PCR_CLEAR_CRG_ERROR
    0xE1A6  PCR_SET_PRINTED_PAGES
    0xE1A7  PCR_EJECT_PAPER
    0xE1A8  PCR_SERVICE_OPERATION
    0xE1A9  PCR_SELECT_PAPER_INPUT
    0xE2A0  PCR_ANNOUNCE_PAGE

## Hardware probe 1 (2026-11-13, idle printer, no paper loaded, nothing printed)
IEEE-1284 ID: `MFG:Canon;MDL:LBP6200;CMD:CAPT;VER:3.0;CLS:PRINTER;DES:Canon LBP6200`
USB: 04a9:271b, 1 interface class 7/1/2, bulk EP1 OUT + EP2 IN (512 B), iSerial 0000A1DATDXT.
| query | reply bytes |
|---|---|
| A0A8 ext status2 | 92 B: `a8a0 5c00 338e 0000 0f00 0000 0000 8000 0080 ...` status word 0x8e33 |
| E0A0 basic status | `a0e0 0600 8800` |
| A1A1 printer info | 64 B `a1a1 4000 000b e309 0101 f0ff 0001 2400 5d00 0103 1806 c003 6f08 6f08 e40d 0000 e40d 0000 fa02 3408 f604 ea0a 3232 3232 5802 5802 1e07 0400 0010 ...` |
|   | decodes plausibly: 0x086f=2159 (Letter width, 0.1 mm), 0x0de4=3556 (Legal), 0x0258=600 dpi x2 |
| A0A4 cartridge status | 20 B `a4a0 1400 000c 0000 0000 0003 0301 0dfe 0000 0000` |
| A1AA engine id | `aaa1 0600 8000` |
| A1A8 engine version | `a8a1 1200 0000 44434f4e3a30342e3034 0000` = "DCON:04.04" |
| A0A0 ext status (old) | `a0a0 0600 8000` |
| A0A1 input status | 22 B `a1a0 1600 0001 c000 0000 0000 0000 9600 fd00 0000 0000` |
| A0A2 output status | `a2a0 0600 8000` |

## Open questions
* Meaning of status word 0x0080 (A0A0, A0A2, A1AA) - "not supported" vs "busy"?
* Full A0A8 field map (CAPT 3.0); compare Windows cnabdemk/captmoncnabd decoders.
* A0A8 status word 0x8e33 bit 1 (paper out in captdriver SPECS) is consistent with no paper loaded.
