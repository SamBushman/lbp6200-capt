#!/bin/sh
# Removes everything install-userspace.sh (and the kext step) put in place.  usage (as root): sh uninstall.sh
rm -rf /Library/Printers/lbp6200 "/Library/Printers/PPDs/Contents/Resources/en.lproj/Canon LBP6200.gz"
if [ -d "/System/Library/Extensions/LBP6200 USB Printing.kext" ]; then
	kextunload -b org.lbp6200capt.driver.PrinterMerge 2>&1 || true
	rm -rf "/System/Library/Extensions/LBP6200 USB Printing.kext"
	touch /System/Library/Extensions     # let kextd refresh its caches
fi
echo removed
