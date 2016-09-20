Summary: SILC Client
Name: silc-client
Version: 1.1.11
Release: fc20
License: GPL
Group: Applications/Communications
URL: http://silcnet.org/
Source0: silc-client-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-buildroot
BuildRequires: libsilc-devel
Requires: libsilc >= 1.1

%description
SILC (Secure Internet Live Conferencing) is a modern and secure
conferencing protocol providing secure private messages, channel
messages and video and audio conferencing.  SILC Client is a
command line client used to connect to SILC server in SILC network.

%prep
%setup -q

%build
%configure --prefix=%{_prefix} \
           --mandir=%{_mandir} \
           --infodir=%{_infodir} \
           --bindir=%{_bindir} \
           --datadir=%{_datadir} \
	   --with-perl-lib=%{_libdir}/silc/perl5 \
           --enable-ipv6 --with-socks --with-perl=yes
make -j4

%install
rm -rf $RPM_BUILD_ROOT
make DESTDIR=$RPM_BUILD_ROOT install
rm -f $RPM_BUILD_ROOT/%{_libdir}/silc/perl5/*/perllocal.pod
mv $RPM_BUILD_ROOT/%{_datadir}/doc/silc-client \
  $RPM_BUILD_ROOT/%{_datadir}/doc/silc-client-%version

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr (755,root,root,755)
%{_bindir}/*
%{_libdir}/silc/perl5
%defattr (644,root,root,755)
%{_sysconfdir}/silc.conf
%{_mandir}/man1/*
%{_datadir}/silc
%doc %{_datadir}/doc

%changelog
* Sun Jun  3 2007 - Pekka Riikonen <priikone@silcnet.org>
- Initial version
