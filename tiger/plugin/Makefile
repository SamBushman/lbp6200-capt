# Mac OS X 10.4 (PPC) only: builds the plug-in bundle with the Xcode 2.x gcc 4.0.1 and the 10.4u SDK.  Use `nice make` on a shared machine.
SDK ?= /Developer/SDKs/MacOSX10.4u.sdk
OUT ?= build
BUNDLE = $(OUT)/LBP6200 USB ClassDriver.plugin
CFLAGS = -arch ppc -isysroot $(SDK) -mmacosx-version-min=10.4 -std=gnu99 -Wall -Wextra -O2 -Wno-unused-parameter
SRC = lbp_class.c ../../lbpc/lbpc_drive.c ../../core/capt.c ../../hiscoa/hiscoa-compress.c ../../hiscoa/hiscoa-common.c
all: $(OUT)/stamp
$(OUT)/stamp: $(SRC) lbp_usbio.h Info.plist ../../lbpc/lbpc.h ../../core/capt.h
	mkdir -p "$(BUNDLE)/Contents/MacOS"
	cp Info.plist "$(BUNDLE)/Contents/Info.plist"
	gcc $(CFLAGS) -bundle -o "$(BUNDLE)/Contents/MacOS/LBP6200USBClassDriver" $(SRC) -framework CoreFoundation -framework IOKit
	touch $@
