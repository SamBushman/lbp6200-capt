# Installing lbp6200-capt on Bazzite / Fedora Atomic

The package adds five files (all under /usr, so it is a normal rpm-ostree layer):

| file | purpose |
|---|---|
| /usr/lib/cups/filter/rastertocapt6200 | CUPS raster -> CAPT 3.0 (halftone, Hi-SCoA, duplex) |
| /usr/lib/cups/backend/lbp6200 | CUPS backend for the kernel usblp node |
| /usr/share/cups/model/lbp6200c.ppd | PPD |
| /usr/bin/lbp6200-status | read-only status / toner / problem query |
| /usr/share/cups/usb/lbp6200.usb-quirks | hides the printer from CUPS' built-in usb backend (it merges CAPT commands and wedges the printer) |

## Install
    rpm-ostree install ./lbp6200-capt-0.1-1.*.rpm
    systemctl reboot

## Add the printer
Plug the printer in (USB) and check that the kernel made a printer node: `ls -l /dev/usb/lp0`.
Then, as root:

    lpadmin -p LBP6200 -E -v lbp6200:/dev/usb/lp0 -m lbp6200c.ppd
    lpoptions -p LBP6200 -o media=A4        # or Letter

(Use the node your system shows if it is not lp0.)  Duplex: `lp -d LBP6200 -o Duplex=DuplexNoTumble file.pdf`
(long edge) or `DuplexTumble` (short edge).

## Notes
* No udev rule is needed: CUPS runs the backend as root, and systemd already grants the logged-in user an ACL on /dev/usb/lpN
  for `lbp6200-status`.
* The built-in usb:// backend must not be used: besides competing with usblp it merges CAPT commands into one USB message, which wedges the printer
  until it is power-cycled.  The package therefore blacklists 04a9:271b for that backend, so the only device CUPS offers is `lbp6200:/dev/usb/lp0`
  (`lpinfo -v` should show it and no `usb://Canon/LBP6200`).  If a `usb://Canon/LBP6200...` queue exists from an earlier setup, re-point it:
  `lpadmin -p LBP6200 -v lbp6200:/dev/usb/lp0`.
* The printer sleeps; the first job after idle takes a few seconds longer (the driver wakes it).
* `lbp6200-status` prints state, page counters, toner cartridge flags and a `Problems:` line (paper out, cover open, jam,
  service call, toner).  CUPS shows the same conditions as printer-state-reasons while a job runs.
* Not verified on a real Fedora/Bazzite system yet: SELinux labelling of the backend (/usr/lib/cups/backend is expected to be
  labelled by policy automatically).  If printing is denied, check `ausearch -m avc -ts recent`.
* Verified on hardware (development machine): simplex and duplex (long edge), Letter, up to 4 pages.  Other paper sizes and
  short-edge duplex are implemented and checked against Canon's wire format but not yet printed.
