Name:           lbp6200-capt
Version:        0.1
Release:        1%{?dist}
Summary:        Open-source CUPS driver for the Canon LBP6200 (CAPT 3.0)
License:        GPL-3.0-or-later
Source0:        lbp6200-capt-%{version}.tar.gz
BuildRequires:  gcc, make, cups-devel
Requires:       cups, cups-filters

%description
Raster filter, PPD and status tool for the Canon LBP6200/LBP6200d laser printer,
written from scratch from the CAPT 3.0 protocol. Uses the stock CUPS usb backend.

%prep
%autosetup

%build
make -C cups
make -C status

%install
install -Dm755 cups/rastertocapt6200 %{buildroot}%{_prefix}/lib/cups/filter/rastertocapt6200
install -Dm644 ppd/lbp6200c.ppd %{buildroot}%{_datadir}/cups/model/lbp6200c.ppd
install -Dm755 status/lbp6200-status %{buildroot}%{_bindir}/lbp6200-status

%files
%{_prefix}/lib/cups/filter/rastertocapt6200
%{_datadir}/cups/model/lbp6200c.ppd
%{_bindir}/lbp6200-status
