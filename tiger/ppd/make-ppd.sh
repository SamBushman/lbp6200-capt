#!/bin/sh
# Derives the Tiger PPD from the shared source PPD (ppd/lbp6200c.ppd): absolute filter path (Tiger convention), no SW colour space (CUPS 1.1 has none: 8-bit gray = cupsColorSpace 0, W).
# usage: make-ppd.sh > "Canon LBP6200.ppd"
cd "$(dirname "$0")/../.."
sed -e 's|^\*PCFileName:.*|*PCFileName: "CanonLBP6200.ppd"|' \
    -e 's|^\*cupsFilter:.*|*cupsFilter: "application/vnd.cups-raster 100 /Library/Printers/lbp6200/Filter/rastertolbpc"|' \
    -e 's|^\*ShortNickName:.*|*ShortNickName: "Canon LBP6200"|' \
    -e 's|^\*NickName:.*|*NickName: "Canon LBP6200 CAPT (open-source driver)"|' \
    -e 's|cupsColorSpace 18|cupsColorSpace 0|' ppd/lbp6200c.ppd
