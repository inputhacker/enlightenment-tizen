Name:           enlightenment
Version:        0.20.0
Release:        0
License:        BSD-2-Clause
Summary:        The Enlightenment wayland display server
Url:            http://www.enlightenment.org/
Group:          Graphics/EFL
Source0:        enlightenment-%{version}.tar.bz2
Source1001:     enlightenment.manifest

%if 0%{?tizen_version_major} <= 3
# use libgomp only in arm 32bit mobile
%ifarch %{arm}
%if "%{?profile}" == "mobile"
%define LIBGOMP use
%endif
%endif
%endif

BuildRequires:  eet-tools
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(dlog)
BuildRequires:  pkgconfig(ecore)
BuildRequires:  pkgconfig(ecore-evas)
BuildRequires:  pkgconfig(ecore-file)
BuildRequires:  pkgconfig(ecore-input)
BuildRequires:  pkgconfig(edbus)
BuildRequires:  pkgconfig(edje)
BuildRequires:  pkgconfig(eet)
BuildRequires:  pkgconfig(eina)
BuildRequires:  pkgconfig(eio)
BuildRequires:  pkgconfig(evas)
BuildRequires:  pkgconfig(libtbm)
BuildRequires:  pkgconfig(ttrace)
BuildRequires:  pkgconfig(wayland-server)
BuildRequires:  pkgconfig(xdg-shell-server)
BuildRequires:  pkgconfig(scaler-server)
BuildRequires:  pkgconfig(transform-server)
BuildRequires:  pkgconfig(screenshooter-server)
BuildRequires:  pkgconfig(screenshooter-client)
BuildRequires:  pkgconfig(tizen-extension-server)
BuildRequires:  pkgconfig(tizen-launch-server)
BuildRequires:  pkgconfig(wayland-tbm-server)
BuildRequires:  pkgconfig(tizen-remote-surface-server)
BuildRequires:  pkgconfig(ecore-drm)
BuildRequires:  pkgconfig(libtdm)
BuildRequires:  pkgconfig(gbm)
BuildRequires:  pkgconfig(capi-system-device)
BuildRequires:  pkgconfig(tzsh-server)
BuildRequires:  pkgconfig(cynara-client)
BuildRequires:  pkgconfig(cynara-creds-socket)
BuildRequires:  pkgconfig(libsmack)
BuildRequires:  pkgconfig(pixman-1)
BuildRequires:  systemd-devel
Requires:       libwayland-extension-server
%if "%{LIBGOMP}" == "use"
Requires:       libgomp
%endif

%description
Enlightenment is a window manager.

%package devel
Summary:        Development components for the enlightenment package
Group:          Development/Libraries
Requires:       %{name} = %{version}
Requires:       pkgconfig(tizen-extension-server)

%description devel
Development files for enlightenment

%prep
%setup -q -n %{name}-%{version}
cp %{SOURCE1001} .

%build
%if "%{TIZEN_PRODUCT_TV}" != "1"
export CFLAGS+=" -fPIE "
export LDFLAGS+=" -pie "
%endif
%autogen \
      --enable-function-trace \
      --enable-wayland \
      --enable-quick-init \
%if "%{LIBGOMP}" == "use"
      --enable-libgomp \
%endif
      --enable-hwc-multi

make %{?_smp_mflags}

%install
%make_install

%files
%manifest %{name}.manifest
%defattr(-,root,root,-)
%license COPYING
%attr(750,root,root) %{_bindir}/enlightenment*
%{_libdir}/enlightenment/*
%{_datadir}/enlightenment/*
%{_sysconfdir}/dbus-1/system.d/org.enlightenment.wm.conf
%exclude %{_bindir}/enlightenment_remote
%exclude /usr/share/enlightenment/data/config/profile.cfg
%exclude %{_datadir}/enlightenment/data/*
%exclude %{_datadir}/enlightenment/data/

%files devel
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_includedir}/enlightenment/*
%{_libdir}/pkgconfig/*.pc
