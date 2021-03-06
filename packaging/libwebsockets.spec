Name:       libwebsockets
Summary:    WebSocket Library
Version:    support_chrome_20_firefox_12_11_g71e5369
Release:    1
Group:      System/Libraries
License:    LGPLv2.1
URL:        http://git.warmcat.com/cgi-bin/cgit/libwebsockets/
Source0:    %{name}-%{version}.tar.bz2
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
BuildRequires: zlib-devel

%description
C Websockets Server Library

%package devel
Summary:    Development files for %{name}
Group:      Development/Libraries
Requires:   %{name} = %{version}-%{release}

%description devel
Development files needed for building websocket clients and servers

%prep
%setup -q -n %{name}-%{version}

%build

autoreconf -i
%configure

make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%make_install

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_bindir}/libwebsockets*
%{_libdir}/libwebsockets*.so.*
%{_datadir}/libwebsockets-test-server/*

%files devel
%defattr(-,root,root,-)
%{_includedir}/libwebsockets.h
%{_libdir}/libwebsockets.so
