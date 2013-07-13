%define libvers 2.2.8
Summary: QuadStor Storage Virtualization 
Name: quadstor-vtl-core 
Version: 2.2.8
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
cd /quadstorvtl/quadstor/ && sh build.sh clean
cd /quadstorvtl/quadstor/ && sh build.sh

%install
rm -rf $RPM_BUILD_ROOT/

mkdir -p $RPM_BUILD_ROOT/quadstorvtl/lib/modules
mkdir -p $RPM_BUILD_ROOT/quadstorvtl/bin
mkdir -p $RPM_BUILD_ROOT/quadstorvtl/sbin
mkdir -p $RPM_BUILD_ROOT/quadstorvtl/lib
mkdir -p $RPM_BUILD_ROOT/quadstorvtl/lib/modules
mkdir -p $RPM_BUILD_ROOT/quadstorvtl/etc
mkdir -p $RPM_BUILD_ROOT/quadstorvtl/etc/iet
#mkdir -p $RPM_BUILD_ROOT/quadstorvtl/etc/specs
mkdir -p $RPM_BUILD_ROOT/srv/www/cgi-bin
mkdir -p $RPM_BUILD_ROOT/srv/www/htdocs
mkdir -p $RPM_BUILD_ROOT/srv/www/htdocs/quadstorvtl
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/

cd /quadstorvtl/quadstor/pgsql
make install DESTDIR=$RPM_BUILD_ROOT

install -m 755 /quadstorvtl/quadstor/masterd/mdaemon $RPM_BUILD_ROOT/quadstorvtl/sbin/mdaemon
install -m 755  /quadstorvtl/quadstor/library/client/libtlclnt.so $RPM_BUILD_ROOT/quadstorvtl/lib/libtlclnt.so.%{libvers}
install -m 755 /quadstorvtl/quadstor/library/server/libtlsrv.so $RPM_BUILD_ROOT/quadstorvtl/lib/libtlsrv.so.%{libvers}
install -m 755 /quadstorvtl/quadstor/library/common/libtlmsg.so $RPM_BUILD_ROOT/quadstorvtl/lib/libtlmsg.so.%{libvers}
install -m 644 /quadstorvtl/lib/modules/corelib.o $RPM_BUILD_ROOT/quadstorvtl/lib/modules/
install -m 755 /quadstorvtl/quadstor/mapps/html/cgisrc/*.cgi $RPM_BUILD_ROOT/srv/www/cgi-bin/
install -m 755 /quadstorvtl/quadstor/mapps/html/cgisrc/*.css $RPM_BUILD_ROOT/srv/www/htdocs/quadstorvtl/
install -m 755 /quadstorvtl/quadstor/mapps/html/cgisrc/*.js $RPM_BUILD_ROOT/srv/www/htdocs/quadstorvtl/
install -m 755 /quadstorvtl/quadstor/mapps/html/cgisrc/*.png $RPM_BUILD_ROOT/srv/www/htdocs/quadstorvtl/
cp -pr /quadstorvtl/quadstor/mapps/html/cgisrc/yui/ $RPM_BUILD_ROOT/srv/www/htdocs/quadstorvtl/
install -m 755 /quadstorvtl/quadstor/mapps/html/cgisrc/index.html $RPM_BUILD_ROOT/srv/www/htdocs/qsindex.html
install -m 755 /quadstorvtl/quadstor/scctl/scctl $RPM_BUILD_ROOT/quadstorvtl/bin/scctl
install -m 755 /quadstorvtl/quadstor/scctl/fcconfig $RPM_BUILD_ROOT/quadstorvtl/bin/fcconfig
install -m 755 /quadstorvtl/quadstor/scctl/dbrecover $RPM_BUILD_ROOT/quadstorvtl/bin/dbrecover
install -m 744 /quadstorvtl/quadstor/etc/quadstor.linux $RPM_BUILD_ROOT/etc/rc.d/quadstorvtl
install -m 444 /quadstorvtl/quadstor/build/LICENSE $RPM_BUILD_ROOT/quadstorvtl/
install -m 444 /quadstorvtl/quadstor/build/GPLv2 $RPM_BUILD_ROOT/quadstorvtl/

#Install src
mkdir -p $RPM_BUILD_ROOT/quadstorvtl/src/others
cp /quadstorvtl/quadstor/library/server/md5*.[ch] $RPM_BUILD_ROOT/quadstorvtl/src/others/
cp /quadstorvtl/quadstor/core/lz4*.[ch] $RPM_BUILD_ROOT/quadstorvtl/src/others/
cp /quadstorvtl/quadstor/core/lzf*.[ch] $RPM_BUILD_ROOT/quadstorvtl/src/others/
cp /quadstorvtl/quadstor/core/sysdefs/*.h $RPM_BUILD_ROOT/quadstorvtl/src/others/


cd $RPM_BUILD_ROOT/quadstorvtl/lib && ln -fs libtlclnt.so.%{libvers} libtlclnt.so.1
cd $RPM_BUILD_ROOT/quadstorvtl/lib && ln -fs libtlclnt.so.%{libvers} libtlclnt.so
cd $RPM_BUILD_ROOT/quadstorvtl/lib && ln -fs libtlsrv.so.%{libvers} libtlsrv.so.1
cd $RPM_BUILD_ROOT/quadstorvtl/lib && ln -fs libtlsrv.so.%{libvers} libtlsrv.so
cd $RPM_BUILD_ROOT/quadstorvtl/lib && ln -fs libtlmsg.so.%{libvers} libtlmsg.so.1
cd $RPM_BUILD_ROOT/quadstorvtl/lib && ln -fs libtlmsg.so.%{libvers} libtlmsg.so

%pre
	if [ -f /srv/www/htdocs/index.html ];then
		mv -f /srv/www/htdocs/index.html /srv/www/htdocs/index.html.ssave
	fi

%post
	echo "Performing post install. Please wait..."
	sleep 2

	if [ -d /quadstor/pgsql/data -a ! -d /quadstorvtl/pgsql/data ]; then
		vtl=`grep -r vtl /quadstor/pgsql/data/base/* 2> /dev/null`
		tdisk=`grep -r tdisk /quadstor/pgsql/data/base/* 2> /dev/null`
		if [ "$vtl" != "" -a "$tdisk" = "" ]; then
			echo "WARNING: Moving /quadstor/pgsql/data to /quadstorvtl/pgsql/"
			mv -f /quadstor/pgsql/data /quadstorvtl/pgsql/data
		fi
	fi

	if [ ! -d /quadstorvtl/pgsql/data ]; then
		/quadstorvtl/pgsql/scripts/pgpost.sh
	fi

	/quadstorvtl/pgsql/scripts/pgpatch.sh > /dev/null 2>&1


	/sbin/chkconfig --add quadstor > /dev/null 2>&1

	/usr/sbin/setsebool -P httpd_enable_cgi 1 > /dev/null 2>&1

	cp -f /srv/www/htdocs/qsindex.html /srv/www/htdocs/index.html
	mkdir -p /quadstorvtl/etc
	echo "2.2.8 for SLES 11 SP1" > /quadstorvtl/etc/quadstor-vtl-core-version
	exit 0

%preun
	/sbin/chkconfig --del quadstor > /dev/null 2>&1

	cmod=`/sbin/lsmod | grep vtlcore`
	if [ "$cmod" != "" ]; then
		/etc/rc.d/quadstorvtl stop
	fi

	cmod=`/sbin/lsmod | grep vtlcore`
	if [ "$cmod" != "" ]; then
		echo "Unable to shutdown QUADStor service cleanly. Please restart the system and try again"
		exit 1
	fi


%postun
	rm -f /quadstorvtl/etc/quadstor-vtl-core-version
	if [ -f /srv/www/htdocs/index.html.ssave ];then
		mv -f /srv/www/htdocs/index.html.ssave /srv/www/htdocs/index.html
	fi
	rm -rf /quadstorvtl/sbin /quadstorvtl/share /quadstorvtl/src/others/
	rmdir --ignore-fail-on-non-empty /quadstorvtl/lib/modules > /dev/null 2>&1
	rmdir --ignore-fail-on-non-empty /quadstorvtl/lib > /dev/null 2>&1
	rmdir --ignore-fail-on-non-empty /quadstorvtl/bin > /dev/null 2>&1
	rmdir --ignore-fail-on-non-empty /quadstorvtl/etc > /dev/null 2>&1
	exit 0

%files
%defattr(-,root,root)
/quadstorvtl/sbin/mdaemon
/quadstorvtl/bin/scctl
/quadstorvtl/bin/fcconfig
/quadstorvtl/bin/dbrecover
/quadstorvtl/lib/libtlclnt.so.1
/quadstorvtl/lib/libtlclnt.so.%{libvers}
/quadstorvtl/lib/libtlclnt.so
/quadstorvtl/lib/libtlsrv.so
/quadstorvtl/lib/libtlsrv.so.1
/quadstorvtl/lib/libtlsrv.so.%{libvers}
/quadstorvtl/lib/libtlmsg.so
/quadstorvtl/lib/libtlmsg.so.1
/quadstorvtl/lib/libtlmsg.so.%{libvers}
/quadstorvtl/lib/modules/corelib.o
/srv/www/cgi-bin/*.cgi
/srv/www/htdocs/quadstorvtl/
/srv/www/htdocs/qsindex.html
/etc/rc.d/quadstorvtl

#pgsql files
/quadstorvtl/pgsql/bin/
/quadstorvtl/pgsql/lib/
/quadstorvtl/pgsql/share/
/quadstorvtl/pgsql/scripts/
/quadstorvtl/pgsql/etc/

#src files
/quadstorvtl/src/

#License
/quadstorvtl/LICENSE
/quadstorvtl/GPLv2

%clean
rm -rf $RPM_BUILD_ROOT/
