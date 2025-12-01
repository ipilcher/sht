#
# These are set by copr.sh.
#
%global		libver	@@LIBVER@@
%global		sover	@@SOVER@@

Name:		libsht
Summary:	Simple hash table for C programs
Version:	%libver
Release:	1%{?dist}
License:	GPL-3.0-or-later
URL:		https://github.com/ipilcher/sht
Source:		%{name}-%{version}.tar.gz
BuildRequires:	gcc

%description
A simple hash table library for C programs.

%package devel
Summary:	Development files for libsht
Requires:	%{name}%{?_isa} = %{version}-%{release}
%description devel
Header file for libsht development.

%prep
%autosetup -n sht

%build
cd src
%__cc %optflags -Wextra -Wcast-qual -Wcast-align=strict -fPIC -shared \
	-Wl,-soname,libsht.so.%{sover} -o libsht.so.%{libver} sht.c

%install
%__mkdir_p %{buildroot}%{_libdir}
%__cp src/libsht.so.%{libver} %{buildroot}%{_libdir}/
%__ln_s libsht.so.%{libver} %{buildroot}%{_libdir}/libsht.so.%{sover}
%__mkdir_p %{buildroot}%{_includedir}
%__cp src/sht.h %{buildroot}%{_includedir}/
%__ln_s libsht.so.%{libver} %{buildroot}%{_libdir}/libsht.so

%files
%license LICENSE
%attr(0755, root, root) %{_libdir}/libsht.so.%{libver}
%attr(-, root, root) %{_libdir}/libsht.so.%{sover}

%files devel
%attr(0644, root, root) %{_includedir}/sht.h
%attr(0644, root, root) %{_includedir}/sht-ts.h
%attr(-, root, root) %{_libdir}/libsht.so

%changelog
* Mon Dec 1 2025 Ian Pilcher <arequipeno@gmail.com>
- Add type-safe API header

* Sat Nov 8 2025 Ian Pilcher <arequipeno@gmail.com>
- Initial SPEC file
