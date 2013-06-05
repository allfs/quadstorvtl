%define libvers 2.2.2
Summary: QuadStor Storage Virtualization 
Name: quadstor-vtl-core 
Version: 2.2.2
Release: sles11sp1
Source0: %{name}-%{version}.tar.gz
License: None 
Group: DataStorage 
Vendor: QUADStor Systems 
URL: http://www.quadstor.com
Requires: apache2, tar, coreutils, sg3_utils, util-linux
BuildRoot: /var/tmp/%{name}-buildroot
Provides: libtlclnt.so()(64bit) libtlmsg.so()(64bit) libtlsrv.so()(64bit)
%description
 QUADStor storage virtualization, data deduplication 
%build
cd /quadstor/quadstor/ && sh build.sh clean
cd /quadstor/quadstor/ && sh build.sh

%install
rm -rf $RPM_BUILD_ROOT/

mkdir -p $RPM_BUILD_ROOT/quadstor/lib/modules
mkdir -p $RPM_BUILD_ROOT/quadstor/bin
mkdir -p $RPM_BUILD_ROOT/quadstor/sbin
mkdir -p $RPM_BUILD_ROOT/quadstor/lib
mkdir -p $RPM_BUILD_ROOT/quadstor/lib/modules
mkdir -p $RPM_BUILD_ROOT/quadstor/etc
mkdir -p $RPM_BUILD_ROOT/quadstor/etc/iet
#mkdir -p $RPM_BUILD_ROOT/quadstor/etc/specs
mkdir -p $RPM_BUILD_ROOT/srv/www/cgi-bin
mkdir -p $RPM_BUILD_ROOT/srv/www/htdocs
mkdir -p $RPM_BUILD_ROOT/srv/www/htdocs/quadstor
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/

cd /quadstor/quadstor/pgsql
make install DESTDIR=$RPM_BUILD_ROOT

install -m 755 /quadstor/quadstor/masterd/mdaemon $RPM_BUILD_ROOT/quadstor/sbin/mdaemon
install -m 755  /quadstor/quadstor/library/client/libtlclnt.so $RPM_BUILD_ROOT/quadstor/lib/libtlclnt.so.%{libvers}
install -m 755 /quadstor/quadstor/library/server/libtlsrv.so $RPM_BUILD_ROOT/quadstor/lib/libtlsrv.so.%{libvers}
install -m 755 /quadstor/quadstor/library/common/libtlmsg.so $RPM_BUILD_ROOT/quadstor/lib/libtlmsg.so.%{libvers}
install -m 644 /quadstor/lib/modules/corelib.o $RPM_BUILD_ROOT/quadstor/lib/modules/
install -m 755 /quadstor/quadstor/mapps/html/cgisrc/*.cgi $RPM_BUILD_ROOT/srv/www/cgi-bin/
install -m 755 /quadstor/quadstor/mapps/html/cgisrc/*.css $RPM_BUILD_ROOT/srv/www/htdocs/quadstor/
install -m 755 /quadstor/quadstor/mapps/html/cgisrc/*.js $RPM_BUILD_ROOT/srv/www/htdocs/quadstor/
install -m 755 /quadstor/quadstor/mapps/html/cgisrc/*.png $RPM_BUILD_ROOT/srv/www/htdocs/quadstor/
cp -pr /quadstor/quadstor/mapps/html/cgisrc/yui/ $RPM_BUILD_ROOT/srv/www/htdocs/quadstor/
install -m 755 /quadstor/quadstor/mapps/html/cgisrc/index.html $RPM_BUILD_ROOT/srv/www/htdocs/qsindex.html
install -m 755 /quadstor/quadstor/scctl/scctl $RPM_BUILD_ROOT/quadstor/bin/scctl
install -m 755 /quadstor/quadstor/scctl/fcconfig $RPM_BUILD_ROOT/quadstor/bin/fcconfig
install -m 755 /quadstor/quadstor/scctl/dbrecover $RPM_BUILD_ROOT/quadstor/bin/dbrecover
install -m 744 /quadstor/quadstor/etc/quadstor.linux $RPM_BUILD_ROOT/etc/rc.d/quadstor
install -m 444 /quadstor/quadstor/build/LICENSE $RPM_BUILD_ROOT/quadstor/
install -m 444 /quadstor/quadstor/build/GPLv2 $RPM_BUILD_ROOT/quadstor/

#Install src
mkdir -p $RPM_BUILD_ROOT/quadstor/src/others
cp /quadstor/quadstor/library/server/md5*.[ch] $RPM_BUILD_ROOT/quadstor/src/others/
cp /quadstor/quadstor/core/lz4*.[ch] $RPM_BUILD_ROOT/quadstor/src/others/
cp /quadstor/quadstor/core/lzf*.[ch] $RPM_BUILD_ROOT/quadstor/src/others/
cp /quadstor/quadstor/core/sysdefs/*.h $RPM_BUILD_ROOT/quadstor/src/others/


cd $RPM_BUILD_ROOT/quadstor/lib && ln -fs libtlclnt.so.%{libvers} libtlclnt.so.1
cd $RPM_BUILD_ROOT/quadstor/lib && ln -fs libtlclnt.so.%{libvers} libtlclnt.so
cd $RPM_BUILD_ROOT/quadstor/lib && ln -fs libtlsrv.so.%{libvers} libtlsrv.so.1
cd $RPM_BUILD_ROOT/quadstor/lib && ln -fs libtlsrv.so.%{libvers} libtlsrv.so
cd $RPM_BUILD_ROOT/quadstor/lib && ln -fs libtlmsg.so.%{libvers} libtlmsg.so.1
cd $RPM_BUILD_ROOT/quadstor/lib && ln -fs libtlmsg.so.%{libvers} libtlmsg.so

%pre
	if [ -f /srv/www/htdocs/index.html ];then
		mv -f /srv/www/htdocs/index.html /srv/www/htdocs/index.html.ssave
	fi

%post
	echo "Performing post install. Please wait..."
	sleep 2
	if [ ! -d /quadstor/pgsql/data ]; then
		/quadstor/pgsql/scripts/pgpost.sh
	fi

	/quadstor/pgsql/scripts/pgpatch.sh > /dev/null 2>&1


	/sbin/chkconfig --add quadstor > /dev/null 2>&1

	/usr/sbin/setsebool -P httpd_enable_cgi 1 > /dev/null 2>&1

	cp -f /srv/www/htdocs/qsindex.html /srv/www/htdocs/index.html
	mkdir -p /quadstor/etc
	echo "2.2.2 for SLES 11 SP1" > /quadstor/etc/quadstor-vtl-core-version
	exit 0

%preun
	/sbin/chkconfig --del quadstor > /dev/null 2>&1

	cmod=`/sbin/lsmod | grep vtlcore`
	if [ "$cmod" != "" ]; then
		/etc/rc.d/quadstor stop
	fi

	cmod=`/sbin/lsmod | grep vtlcore`
	if [ "$cmod" != "" ]; then
		echo "Unable to shutdown QUADStor service cleanly. Please restart the system and try again"
		exit 1
	fi


%postun
	rm -f /quadstor/etc/quadstor-vtl-core-version
	if [ -f /srv/www/htdocs/index.html.ssave ];then
		mv -f /srv/www/htdocs/index.html.ssave /srv/www/htdocs/index.html
	fi
	rm -rf /quadstor/sbin /quadstor/share /quadstor/src/others/
	rmdir --ignore-fail-on-non-empty /quadstor/lib/modules > /dev/null 2>&1
	rmdir --ignore-fail-on-non-empty /quadstor/lib > /dev/null 2>&1
	rmdir --ignore-fail-on-non-empty /quadstor/bin > /dev/null 2>&1
	rmdir --ignore-fail-on-non-empty /quadstor/etc > /dev/null 2>&1
	exit 0

%files
%defattr(-,root,root)
/quadstor/sbin/mdaemon
/quadstor/bin/scctl
/quadstor/bin/fcconfig
/quadstor/bin/dbrecover
/quadstor/lib/libtlclnt.so.1
/quadstor/lib/libtlclnt.so.%{libvers}
/quadstor/lib/libtlclnt.so
/quadstor/lib/libtlsrv.so
/quadstor/lib/libtlsrv.so.1
/quadstor/lib/libtlsrv.so.%{libvers}
/quadstor/lib/libtlmsg.so
/quadstor/lib/libtlmsg.so.1
/quadstor/lib/libtlmsg.so.%{libvers}
/quadstor/lib/modules/corelib.o
/srv/www/cgi-bin/*.cgi
/srv/www/htdocs/quadstor/
/srv/www/htdocs/qsindex.html
/etc/rc.d/quadstor

#pgsql files
/quadstor/pgsql/bin/
/quadstor/pgsql/lib/
/quadstor/pgsql/share/
/quadstor/pgsql/scripts/
/quadstor/pgsql/etc/

#src files
/quadstor/src/

#License
/quadstor/LICENSE
/quadstor/GPLv2

%clean
rm -rf $RPM_BUILD_ROOT/


