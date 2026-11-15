# Installing the LBP6200 driver on Mac OS X 10.4 Tiger (PowerPC)

Tested on a Power Mac G5 / Tiger 10.4.11 with CUPS 1.1.23, Canon LBP6200 on USB.  Paper tested: **Letter** (A4 and short-edge duplex are not tested).

## Install
1. Unpack `LBP6200-CAPT-0.1.0.pkg` (distributed as a .tar.gz of the package bundle) and double-click it, or run `sudo installer -pkg LBP6200-CAPT-0.1.0.pkg -target /`.
2. Plug in the printer and add it: Print Setup Utility / System Preferences -> Print & Fax -> Add (+).  The printer appears as **Canon LBP6200** (USB); choose the driver **Canon LBP6200 CAPT (open-source driver)** if it is not selected automatically.  Command line: `sudo lpadmin -p LBP6200 -E -v "usb://Canon/LBP6200?serial=<serial>" -m "Canon LBP6200.gz"` (the exact URI is listed by `/usr/libexec/cups/backend/usb`).
3. No restart is needed: the installer loads the USB match personality into the running system.  (If that fails the personality is loaded from /System/Library/Extensions at the next boot.)

## What is installed
| Path | Purpose |
|---|---|
| /Library/Printers/lbp6200/Filter/rastertolbpc | CUPS raster filter (raster -> LBPC records) |
| /Library/Printers/lbp6200/LBP6200 USB ClassDriver.plugin | USB class driver loaded by Apple's usb backend: runs the CAPT conversation with the printer |
| /Library/Printers/PPDs/Contents/Resources/en.lproj/Canon LBP6200.gz | PPD |
| /System/Library/Extensions/LBP6200 USB Printing.kext | codeless kext: tells the usb backend to use the class driver for this printer |
| /Library/Printers/lbp6200/uninstall.sh | removes everything |

## Uninstall
`sudo sh /Library/Printers/lbp6200/uninstall.sh` (also removes the kext; delete the queue with `lpadmin -x LBP6200`).

## Troubleshooting
* A failed job makes the usb backend exit with an error and CUPS **disables the queue**: `sudo /usr/bin/enable LBP6200`.
* The printer keeps the state of an aborted job: if the next job fails at once, switch the printer off and on.
* Cancelling a job in the middle of a sheet can eject that sheet blank (normal).
* Build the package yourself on a Tiger Mac with the Xcode tools: `sh tiger/pkg/build-pkg.sh`.
* Diagnostic build: compile the plug-in with `-DLBP_TRACE='"/path/trace.log"'` to log printer status changes and every USB write.

## Verified (Nov 15 2026, G5 / Tiger 10.4.11)
* Package install over an existing manual install, then a restart: the kext personality loaded at boot by itself (`USB Printing Class` on the printer's interface), the queue survived, and a Letter page printed without any manual step.

## Not verified
* A clean install of the .pkg on a machine that never had the driver.
* Switching the printer on only *after* the machine has booted (it was already on when checked).
