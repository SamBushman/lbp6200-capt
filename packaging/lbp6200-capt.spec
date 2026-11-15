Name:           lbp6200-capt
Version:        0.1
Release:        4%{?dist}
Summary:        Open-source CUPS driver for the Canon LBP6200 (CAPT 3.0)
License:        GPL-3.0-or-later
Source0:        lbp6200-capt-%{version}.tar.gz
BuildRequires:  gcc, make, cups-devel
Requires:       cups, cups-filters

%description
CUPS raster filter, CUPS backend, PPD and status tool for the Canon LBP6200/LBP6200d
monochrome laser printer, written from scratch from the CAPT 3.0 protocol (no Canon code).
Supports duplex (long and short edge), 600 dpi with Canon's Hi-SCoA compression, and
reports paper/cover/jam/toner problems as CUPS printer-state-reasons.

A usb-backend quirk hides the printer from CUPS' built-in usb backend (it cannot drive this printer).
The backend talks to the kernel usblp node (/dev/usb/lpN), so the printer must be bound to
usblp (default on Fedora).  Add the queue with:
  lpadmin -p LBP6200 -E -v lbp6200:/dev/usb/lp0 -m lbp6200c.ppd

%prep
%autosetup

%build
make -C cups CFLAGS="%{optflags} -std=gnu99"
make -C status CFLAGS="%{optflags} -std=gnu99"

%install
install -Dm755 cups/rastertocapt6200 %{buildroot}%{_prefix}/lib/cups/filter/rastertocapt6200
install -Dm700 cups/backend-lbp6200 %{buildroot}%{_prefix}/lib/cups/backend/lbp6200
install -Dm644 cups/lbp6200.usb-quirks %{buildroot}%{_datadir}/cups/usb/lbp6200.usb-quirks
install -Dm644 ppd/lbp6200c.ppd %{buildroot}%{_datadir}/cups/model/lbp6200c.ppd
install -Dm755 status/lbp6200-status %{buildroot}%{_bindir}/lbp6200-status
install -Dm644 doc/INSTALL-bazzite.md %{buildroot}%{_docdir}/%{name}/INSTALL-bazzite.md

%files
%license COPYING
%doc %{_docdir}/%{name}/INSTALL-bazzite.md
%{_prefix}/lib/cups/filter/rastertocapt6200
%attr(0700,root,root) %{_prefix}/lib/cups/backend/lbp6200
%{_datadir}/cups/model/lbp6200c.ppd
%{_datadir}/cups/usb/lbp6200.usb-quirks
%{_bindir}/lbp6200-status

%changelog
* Sun Nov 15 2026 lbp6200-capt maintainers - 0.1-4
- Internal restructuring shared with the new Mac OS X Tiger port: the page compressor and the raster-to-print-line producer are separate units
  (core/capt.c capt_compress_page, cups/rasterpage.c).  Wire output is byte-identical to 0.1-3 (18 golden scenarios, also under qemu-ppc and ASAN/UBSAN).
- A status query that meets stale replies of an earlier aborted job (wrong command echo) now drains the pipe and is repeated once instead of failing the job.
- A failing data chunk stops the page at once (a later successful chunk could previously overwrite the error).
- lbp6200-status prints the decoded problem conditions (no paper, jam, cover open, toner ...).

* Sat Nov 14 2026 lbp6200-capt maintainers - 0.1-3
- Release the printer on every exit once it is reserved (Canon PrintJob2 behaviour, docs/12): failed job start / failed page -> final E1A1 (state 5, or 4 on
  cancel; never 6) while the link answers, then E0A9.  SIGTERM (CUPS cancel) is handled by the filter and ignored by the backend until the filter is done.
  A page whose raster input ends early is completed with white, reported as an error and ends with state 5.

* Sat Nov 14 2026 lbp6200-capt maintainers - 0.1-2
- Ship a CUPS usb-backend quirk (blacklist 04a9:271b): the built-in usb backend merges CAPT commands into one USB message and wedges the
  printer; only the lbp6200 backend may drive it.  Fixes a fresh setup picking usb://Canon/LBP6200.

* Sat Nov 14 2026 lbp6200-capt maintainers - 0.1-1
- Initial package: filter, usblp backend, PPD, lbp6200-status
