#!/bin/sh
# Builds the Tiger installer package on a Tiger PPC Mac (needs the Xcode tools: gcc 4.0.1, packagemaker; sudo for root ownership).
# usage: sh tiger/pkg/build-pkg.sh [OUT_DIR]       -> OUT_DIR/LBP6200-CAPT-<version>.pkg  (default OUT_DIR = tiger/pkg/out)
set -e
SRC=$(cd "$(dirname "$0")/../.." && pwd); PK=$SRC/tiger/pkg
OUT=${1:-$PK/out}; VER=0.1.0
W=$OUT/work
export PATH=/Developer/Tools:$PATH
sudo rm -rf "$W"; mkdir -p "$W/root" "$W/res"
(cd "$SRC/tiger" && rm -f rastertolbpc && make CUPSIMAGE=-lcupsimage)
(cd "$SRC/tiger/plugin" && rm -rf build && make)
R=$W/root; L=$R/Library/Printers/lbp6200
mkdir -p "$L/Filter" "$R/Library/Printers/PPDs/Contents/Resources/en.lproj" "$R/System/Library/Extensions"
cp "$SRC/tiger/rastertolbpc" "$L/Filter/"
cp -R "$SRC/tiger/plugin/build/LBP6200 USB ClassDriver.plugin" "$L/"
cp "$SRC/tiger/install/uninstall.sh" "$L/"
sh "$SRC/tiger/ppd/make-ppd.sh" | gzip -9 -c > "$R/Library/Printers/PPDs/Contents/Resources/en.lproj/Canon LBP6200.gz"
cp -R "$SRC/tiger/kext/LBP6200 USB Printing.kext" "$R/System/Library/Extensions/"
cp "$PK/resources/postflight" "$W/res/"
# ownership/modes: payload files root:wheel (the usb backend runs as root and loads the plug-in); directories that already exist on every system keep their usual owner
find "$R" -type d -exec chmod 755 {} \; ; find "$R" -type f -exec chmod 644 {} \;
chmod 755 "$L/Filter/rastertolbpc" "$L/LBP6200 USB ClassDriver.plugin/Contents/MacOS/LBP6200USBClassDriver" "$L/uninstall.sh"
sudo chown -R root:wheel "$R"
sudo chown root:admin "$R/Library" "$R/Library/Printers" "$R/Library/Printers/PPDs" "$R/Library/Printers/PPDs/Contents" "$R/Library/Printers/PPDs/Contents/Resources" "$R/Library/Printers/PPDs/Contents/Resources/en.lproj"
sudo chmod 775 "$R/Library" "$R/Library/Printers" "$R/Library/Printers/PPDs" "$R/Library/Printers/PPDs/Contents" "$R/Library/Printers/PPDs/Contents/Resources" "$R/Library/Printers/PPDs/Contents/Resources/en.lproj"
sudo chmod 664 "$R/Library/Printers/PPDs/Contents/Resources/en.lproj/Canon LBP6200.gz"; sudo chown root:admin "$R/Library/Printers/PPDs/Contents/Resources/en.lproj/Canon LBP6200.gz"
mkdir -p "$OUT"
sudo packagemaker -build -p "$OUT/LBP6200-CAPT-$VER.pkg" -f "$R" -r "$W/res" -i "$PK/Info.plist" -d "$PK/Description.plist" -ds
echo "built $OUT/LBP6200-CAPT-$VER.pkg"
