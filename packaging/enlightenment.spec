Name:           enlightenment
Version:        0.20.0
Release:        0
License:        BSD-2-Clause
Summary:        The Enlightenment wayland display server
Url:            http://www.enlightenment.org/
Group:          Graphics/EFL
Source0:        enlightenment-%{version}.tar.bz2
Source1001:     enlightenment.manifest

%define TIZEN_REL_VERSION 1

%ifarch %{arm}
%define LIBGOMP use
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
BuildRequires:  pkgconfig(eeze)
BuildRequires:  pkgconfig(libtbm)
BuildRequires:  pkgconfig(ttrace)
BuildRequires:  pkgconfig(wayland-server)
BuildRequires:  pkgconfig(xdg-shell-server)
BuildRequires:  pkgconfig(xdg-shell-unstable-v6-server)
BuildRequires:  pkgconfig(scaler-server)
BuildRequires:  pkgconfig(screenshooter-server)
BuildRequires:  pkgconfig(screenshooter-client)
BuildRequires:  pkgconfig(tizen-extension-server)
BuildRequires:  pkgconfig(tizen-launch-server)
BuildRequires:  pkgconfig(wayland-tbm-server)
BuildRequires:  pkgconfig(tizen-remote-surface-server)
BuildRequires:  pkgconfig(tizen-surface-server)
BuildRequires:  pkgconfig(eom-server)
BuildRequires:  pkgconfig(libtdm)
BuildRequires:  pkgconfig(gbm)
BuildRequires:  pkgconfig(capi-system-device)
BuildRequires:  pkgconfig(tzsh-server)
BuildRequires:  pkgconfig(cynara-client)
BuildRequires:  pkgconfig(cynara-creds-socket)
BuildRequires:  pkgconfig(libsmack)
BuildRequires:  pkgconfig(pixman-1)
BuildRequires:  pkgconfig(aul)
BuildRequires:  pkgconfig(pkgmgr-info)
BuildRequires:  systemd-devel
BuildRequires:  pkgconfig(libinput)
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
%if "%{tizen_profile_name}" != "tv"
export CFLAGS+=" -fPIE "
export LDFLAGS+=" -pie "
%endif
%autogen \
      TIZEN_REL_VERSION="%{release}-%{TIZEN_REL_VERSION}" \
      --enable-function-trace \
      --enable-wayland \
      --enable-quick-init

make %{?_smp_mflags}

%install
%make_install
ln -sf %{_bindir}/enlightenment_info %{buildroot}%{_bindir}/winfo

%files
%manifest %{name}.manifest
%defattr(-,root,root,-)
%license COPYING
%attr(550,root,root) %{_bindir}/enlightenment*
%attr(550,root,root) %{_bindir}/winfo*
%{_libdir}/enlightenment/*
%{_datadir}/enlightenment/*
%exclude %{_bindir}/enlightenment_remote
%exclude /usr/share/enlightenment/data/config/profile.cfg
%exclude %{_datadir}/enlightenment/data/*
%exclude %{_datadir}/enlightenment/data/

%files devel
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_includedir}/enlightenment/*
%{_libdir}/pkgconfig/*.pc
