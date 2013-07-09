%define quadstor_prereq  apache

Summary: QuadStor Storage Virtualization 
Name: quadstor-vtl-itf
Version: 2.2.8
Release: rhel5
Source0: %{name}-%{version}.tar.gz
License: None 
Group: DataStorage 
Vendor: QUADStor Systems 
URL: http://www.quadstor.com
Requires: kernel-devel, make, gcc, perl
BuildRoot: /var/tmp/%{name}-buildroot
Provides: libtlclnt.so()(64bit) libtlmsg.so()(64bit) libtlsrv.so()(64bit)
%description
 QUADStor storage virtualization, data deduplication 
%build
cd /quadstor/quadstor/ && sh build.sh clean
exit 0

%install
rm -rf $RPM_BUILD_ROOT/

mkdir -p $RPM_BUILD_ROOT/etc/udev/rules.d
mkdir -p $RPM_BUILD_ROOT/quadstor/src
mkdir -p $RPM_BUILD_ROOT/quadstor/bin
mkdir -p $RPM_BUILD_ROOT/quadstor/src/export
cp /quadstor/quadstor/export/devq.[ch] $RPM_BUILD_ROOT/quadstor/src/export/
cp /quadstor/quadstor/export/linuxdefs.h $RPM_BUILD_ROOT/quadstor/src/export/
cp /quadstor/quadstor/export/exportdefs.h $RPM_BUILD_ROOT/quadstor/src/export/
cp /quadstor/quadstor/export/missingdefs.h $RPM_BUILD_ROOT/quadstor/src/export/
cp /quadstor/quadstor/export/qsio_ccb.h $RPM_BUILD_ROOT/quadstor/src/export/
cp /quadstor/quadstor/export/core_linux.c $RPM_BUILD_ROOT/quadstor/src/export/
cp /quadstor/quadstor/export/ldev_linux.[ch] $RPM_BUILD_ROOT/quadstor/src/export/
cp /quadstor/quadstor/export/Makefile.dist $RPM_BUILD_ROOT/quadstor/src/export/Makefile
cp /quadstor/quadstor/export/queue.h $RPM_BUILD_ROOT/quadstor/src/export/

mkdir -p $RPM_BUILD_ROOT/quadstor/src/common
cp /quadstor/quadstor/common/ioctldefs.h $RPM_BUILD_ROOT/quadstor/src/common/
cp /quadstor/quadstor/common/commondefs.h $RPM_BUILD_ROOT/quadstor/src/common/

mkdir -p $RPM_BUILD_ROOT/quadstor/src/target-mode/iscsi
mkdir -p $RPM_BUILD_ROOT/quadstor/src/target-mode/iscsi/include
mkdir -p $RPM_BUILD_ROOT/quadstor/src/target-mode/iscsi/kernel
mkdir -p $RPM_BUILD_ROOT/quadstor/src/target-mode/iscsi/usr
cp /quadstor/quadstor/target-mode/iscsi/include/*.h $RPM_BUILD_ROOT/quadstor/src/target-mode/iscsi/include/
cp /quadstor/quadstor/target-mode/iscsi/kernel/*.[ch] $RPM_BUILD_ROOT/quadstor/src/target-mode/iscsi/kernel/
cp /quadstor/quadstor/target-mode/iscsi/kernel/Makefile.dist $RPM_BUILD_ROOT/quadstor/src/target-mode/iscsi/kernel/Makefile
cp /quadstor/quadstor/target-mode/iscsi/usr/*.[ch] $RPM_BUILD_ROOT/quadstor/src/target-mode/iscsi/usr/
cp /quadstor/quadstor/target-mode/iscsi/usr/Makefile.dist $RPM_BUILD_ROOT/quadstor/src/target-mode/iscsi/usr/Makefile

mkdir -p $RPM_BUILD_ROOT/quadstor/etc/iet/
install -m 755 /quadstor/quadstor/target-mode/iscsi/etc/targets.allow $RPM_BUILD_ROOT/quadstor/etc/iet/targets.allow.sample
install -m 755 /quadstor/quadstor/target-mode/iscsi/etc/initiators.allow $RPM_BUILD_ROOT/quadstor/etc/iet/initiators.allow.sample
install -m 755 /quadstor/quadstor/target-mode/iscsi/etc/ietd.conf $RPM_BUILD_ROOT/quadstor/etc/iet/ietd.conf.sample
install -m 744 /quadstor/quadstor/target-mode/iscsi/etc/initd/initd.redhat $RPM_BUILD_ROOT/quadstor/etc/initd.iscsi

mkdir -p $RPM_BUILD_ROOT/quadstor/src/target-mode/fc/common
install -m 744 /quadstor/quadstor/target-mode/fc/common/fccommon.c $RPM_BUILD_ROOT/quadstor/src/target-mode/fc/common/
mkdir -p $RPM_BUILD_ROOT/quadstor/src/target-mode/fc/qla2xxx
install -m 744 /quadstor/quadstor/target-mode/fc/qla2xxx/*.[ch] $RPM_BUILD_ROOT/quadstor/src/target-mode/fc/qla2xxx
install -m 744 /quadstor/quadstor/target-mode/fc/qla2xxx/Makefile.dist $RPM_BUILD_ROOT/quadstor/src/target-mode/fc/qla2xxx/Makefile
mkdir -p $RPM_BUILD_ROOT/quadstor/src/target-mode/fc/srpt
install -m 744 /quadstor/quadstor/target-mode/fc/srpt/*.[ch] $RPM_BUILD_ROOT/quadstor/src/target-mode/fc/srpt
install -m 744 /quadstor/quadstor/target-mode/fc/srpt/Makefile.dist $RPM_BUILD_ROOT/quadstor/src/target-mode/fc/srpt/Makefile

install -m 755 /quadstor/quadstor/scripts/builditf.linux.sh $RPM_BUILD_ROOT/quadstor/bin/builditf
install -m 755 /quadstor/quadstor/scripts/qlainst $RPM_BUILD_ROOT/quadstor/bin/qlainst
install -m 755 /quadstor/quadstor/scripts/qlauninst $RPM_BUILD_ROOT/quadstor/bin/qlauninst

%post
	if [ ! -f /quadstor/etc/iet/targets.allow ]; then
		cp /quadstor/etc/iet/targets.allow.sample /quadstor/etc/iet/targets.allow
	fi

	if [ ! -f /quadstor/etc/iet/initiators.allow ]; then
		cp /quadstor/etc/iet/initiators.allow.sample /quadstor/etc/iet/initiators.allow
	fi

	if [ ! -f /quadstor/etc/iet/ietd.conf ]; then
		cp /quadstor/etc/iet/ietd.conf.sample /quadstor/etc/iet/ietd.conf
	fi
	echo "2.2.8 for RHEL/CentOS 5.x" > /quadstor/etc/quadstor-vtl-itf-version
	echo "Building required kernel modules"
	echo "Running /quadstor/bin/builditf"
	sleep 5
	/quadstor/bin/builditf

%preun
	cmod=`/sbin/lsmod | grep vtlcore`
	if [ "$cmod" != "" ]; then
		if [ -f /etc/rc.d/init.d/quadstor ]; then
			/etc/rc.d/init.d/quadstor stop
		else
			/etc/rc.d/quadstor stop
		fi
	fi

	cmod=`/sbin/lsmod | grep vtlcore`
	if [ "$cmod" != "" ]; then
		echo "Unable to shutdown QUADStor service cleanly. Please restart the system and try again"
		exit 1
	fi

	/quadstor/bin/qlauninst
	exit 0

%postun
	rm -f /quadstor/etc/quadstor-vtl-itf-version
	for i in `ls -1d /quadstor/lib/modules/*`; do
		if [ -d $i ]; then
			rm -rf $i > /dev/null 2>&1
		fi
	done
	rm -f /quadstor/bin/ietadm
	rmdir --ignore-fail-on-non-empty /quadstor/lib/modules > /dev/null 2>&1
	rm -rf /quadstor/src/export /quadstor/src/common /quadstor/src/target-mode/
	rmdir --ignore-fail-on-non-empty /quadstor/src > /dev/null 2>&1

%files
%defattr(-,root,root)
/quadstor/src/export/
/quadstor/src/common/
/quadstor/src/target-mode/
/quadstor/etc/iet/ietd.conf.sample
/quadstor/etc/iet/initiators.allow.sample
/quadstor/etc/iet/targets.allow.sample
/quadstor/etc/initd.iscsi
/quadstor/bin/builditf
/quadstor/bin/qlainst
/quadstor/bin/qlauninst

%clean
rm -rf $RPM_BUILD_ROOT/
