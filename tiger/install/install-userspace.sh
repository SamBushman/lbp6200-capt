#!/bin/sh
# Mac OS X 10.4 Tiger: install the userspace parts of the LBP6200 driver (NO kernel interaction): filter, class-driver plug-in, PPD.
# usage (as root):  sh install-userspace.sh STAGE_DIR     STAGE_DIR holds: rastertolbpc  "LBP6200 USB ClassDriver.plugin"  Canon_LBP6200.ppd
# Everything goes under /Library/Printers/lbp6200 (+ the PPD).  Code that the root-run usb backend loads is root:wheel and not group-writable.
set -e
S="$1"; [ -d "$S" ] || { echo "usage: $0 STAGE_DIR" >&2; exit 2; }
D=/Library/Printers/lbp6200
mkdir -p "$D/Filter"
cp "$S/rastertolbpc" "$D/Filter/rastertolbpc"
rm -rf "$D/LBP6200 USB ClassDriver.plugin"
cp -R "$S/LBP6200 USB ClassDriver.plugin" "$D/"
chown -R root:wheel "$D"
chmod 755 "$D" "$D/Filter" "$D/Filter/rastertolbpc"
find "$D/LBP6200 USB ClassDriver.plugin" -type d -exec chmod 755 {} \;
find "$D/LBP6200 USB ClassDriver.plugin" -type f -exec chmod 644 {} \;
chmod 755 "$D/LBP6200 USB ClassDriver.plugin/Contents/MacOS/LBP6200USBClassDriver"
P="/Library/Printers/PPDs/Contents/Resources/en.lproj/Canon LBP6200.gz"
gzip -c "$S/Canon_LBP6200.ppd" > "$P"
chown root:admin "$P"; chmod 664 "$P"
echo "installed under $D and $P"
