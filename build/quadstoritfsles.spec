Summary: QuadStor Virtual Tape Library
Name: quadstor-vtl-itf
Version: 2.2.14
Release: sles11sp1
Source0: %{name}-%{version}.tar.gz
License: None 
Group: DataStorage 
Vendor: QUADStor Systems 
URL: http://www.quadstor.com
Requires: kernel-default-devel, make, gcc, perl
BuildRoot: /var/tmp/%{name}-buildroot
Conflicts: quadstor-itf
%description
 QUADStor virtual tape library
%build
cd /quadstorvtl/quadstor/ && sh build.sh clean
exit 0

%install
rm -rf $RPM_BUILD_ROOT/

mkdir -p $RPM_BUILD_ROOT/etc/udev/rules.d
mkdir -p $RPM_BUILD_ROOT/quadstorvtl/src
mkdir -p $RPM_BUILD_ROOT/quadstorvtl/bin
mkdir -p $RPM_BUILD_ROOT/quadstorvtl/src/export
cp /quadstorvtl/quadstor/export/devq.[ch] $RPM_BUILD_ROOT/quadstorvtl/src/export/
cp /quadstorvtl/quadstor/export/linuxdefs.h $RPM_BUILD_ROOT/quadstorvtl/src/export/
cp /quadstorvtl/quadstor/export/exportdefs.h $RPM_BUILD_ROOT/quadstorvtl/src/export/
cp /quadstorvtl/quadstor/export/missingdefs.h $RPM_BUILD_ROOT/quadstorvtl/src/export/
cp /quadstorvtl/quadstor/export/qsio_ccb.h $RPM_BUILD_ROOT/quadstorvtl/src/export/
cp /quadstorvtl/quadstor/export/core_linux.c $RPM_BUILD_ROOT/quadstorvtl/src/export/
cp /quadstorvtl/quadstor/export/ldev_linux.[ch] $RPM_BUILD_ROOT/quadstorvtl/src/export/
cp /quadstorvtl/quadstor/export/Makefile.dist $RPM_BUILD_ROOT/quadstorvtl/src/export/Makefile
cp /quadstorvtl/quadstor/export/queue.h $RPM_BUILD_ROOT/quadstorvtl/src/export/

mkdir -p $RPM_BUILD_ROOT/quadstorvtl/src/common
cp /quadstorvtl/quadstor/common/ioctldefs.h $RPM_BUILD_ROOT/quadstorvtl/src/common/
cp /quadstorvtl/quadstor/common/commondefs.h $RPM_BUILD_ROOT/quadstorvtl/src/common/

mkdir -p $RPM_BUILD_ROOT/quadstorvtl/src/target-mode/iscsi
mkdir -p $RPM_BUILD_ROOT/quadstorvtl/src/target-mode/iscsi/include
mkdir -p $RPM_BUILD_ROOT/quadstorvtl/src/target-mode/iscsi/kernel
mkdir -p $RPM_BUILD_ROOT/quadstorvtl/src/target-mode/iscsi/usr
cp /quadstorvtl/quadstor/target-mode/iscsi/include/*.h $RPM_BUILD_ROOT/quadstorvtl/src/target-mode/iscsi/include/
cp /quadstorvtl/quadstor/target-mode/iscsi/kernel/*.[ch] $RPM_BUILD_ROOT/quadstorvtl/src/target-mode/iscsi/kernel/
cp /quadstorvtl/quadstor/target-mode/iscsi/kernel/Makefile.dist $RPM_BUILD_ROOT/quadstorvtl/src/target-mode/iscsi/kernel/Makefile
cp /quadstorvtl/quadstor/target-mode/iscsi/usr/*.[ch] $RPM_BUILD_ROOT/quadstorvtl/src/target-mode/iscsi/usr/
cp /quadstorvtl/quadstor/target-mode/iscsi/usr/Makefile.dist $RPM_BUILD_ROOT/quadstorvtl/src/target-mode/iscsi/usr/Makefile

mkdir -p $RPM_BUILD_ROOT/quadstorvtl/etc/iet/
install -m 755 /quadstorvtl/quadstor/target-mode/iscsi/etc/targets.allow $RPM_BUILD_ROOT/quadstorvtl/etc/iet/targets.allow.sample
install -m 755 /quadstorvtl/quadstor/target-mode/iscsi/etc/initiators.allow $RPM_BUILD_ROOT/quadstorvtl/etc/iet/initiators.allow.sample
install -m 755 /quadstorvtl/quadstor/target-mode/iscsi/etc/ietd.conf $RPM_BUILD_ROOT/quadstorvtl/etc/iet/ietd.conf.sample

mkdir -p $RPM_BUILD_ROOT/quadstorvtl/src/target-mode/fc/common
install -m 744 /quadstorvtl/quadstor/target-mode/fc/common/fccommon.c $RPM_BUILD_ROOT/quadstorvtl/src/target-mode/fc/common/
mkdir -p $RPM_BUILD_ROOT/quadstorvtl/src/target-mode/fc/qla2xxx
install -m 744 /quadstorvtl/quadstor/target-mode/fc/qla2xxx/*.[ch] $RPM_BUILD_ROOT/quadstorvtl/src/target-mode/fc/qla2xxx
install -m 744 /quadstorvtl/quadstor/target-mode/fc/qla2xxx/Makefile.dist $RPM_BUILD_ROOT/quadstorvtl/src/target-mode/fc/qla2xxx/Makefile
mkdir -p $RPM_BUILD_ROOT/quadstorvtl/src/target-mode/fc/srpt
install -m 744 /quadstorvtl/quadstor/target-mode/fc/srpt/*.[ch] $RPM_BUILD_ROOT/quadstorvtl/src/target-mode/fc/srpt
install -m 744 /quadstorvtl/quadstor/target-mode/fc/srpt/Makefile.dist $RPM_BUILD_ROOT/quadstorvtl/src/target-mode/fc/srpt/Makefile

install -m 755 /quadstorvtl/quadstor/scripts/builditf.linux.sh $RPM_BUILD_ROOT/quadstorvtl/bin/builditf
install -m 755 /quadstorvtl/quadstor/scripts/qlainst $RPM_BUILD_ROOT/quadstorvtl/bin/qlainst
install -m 755 /quadstorvtl/quadstor/scripts/qlauninst $RPM_BUILD_ROOT/quadstorvtl/bin/qlauninst

%pre
	kbuilddir="/lib/modules/`uname -r`/build/"
	if [ ! -f $kbuilddir/Makefile ]; then
		echo "Kernel build dir $kbuilddir does not seem to be valid. Cannot continue."
		echo "If you have done a kernel upgrade, rebooting might help."
		exit 1
	fi

%post
	if [ ! -f /quadstorvtl/etc/iet/targets.allow ]; then
		cp /quadstorvtl/etc/iet/targets.allow.sample /quadstorvtl/etc/iet/targets.allow
	fi

	if [ ! -f /quadstorvtl/etc/iet/initiators.allow ]; then
		cp /quadstorvtl/etc/iet/initiators.allow.sample /quadstorvtl/etc/iet/initiators.allow
	fi

	if [ ! -f /quadstorvtl/etc/iet/ietd.conf ]; then
		cp /quadstorvtl/etc/iet/ietd.conf.sample /quadstorvtl/etc/iet/ietd.conf
	fi
	echo "2.2.14 for SLES 11 SP1" > /quadstorvtl/etc/quadstor-vtl-itf-version

	echo "Building required kernel modules"
	echo "Running /quadstorvtl/bin/builditf"
	sleep 2
	/quadstorvtl/bin/builditf

%preun
	cmod=`/sbin/lsmod | grep vtlcore`
	if [ "$cmod" != "" ]; then
		if [ -f /etc/rc.d/init.d/quadstorvtl ]; then
			/etc/rc.d/init.d/quadstorvtl stop
		else
			/etc/rc.d/quadstorvtl stop
		fi
	fi

	cmod=`/sbin/lsmod | grep vtlcore`
	if [ "$cmod" != "" ]; then
		echo "Unable to shutdown QUADStor service cleanly. Please restart the system and try again"
		exit 1
	fi

	/quadstorvtl/bin/qlauninst
	exit 0

%postun
	rm -f /quadstorvtl/etc/quadstor-vtl-itf-version
	for i in `ls -1d /quadstorvtl/lib/modules/*`; do
		if [ -d $i ]; then
			rm -rf $i > /dev/null 2>&1
		fi
	done
	rm -f /quadstorvtl/bin/ietadm
	rmdir --ignore-fail-on-non-empty /quadstorvtl/lib/modules > /dev/null 2>&1
	rm -rf /quadstorvtl/src/export /quadstorvtl/src/common /quadstorvtl/src/target-mode/
	rmdir --ignore-fail-on-non-empty /quadstorvtl/src > /dev/null 2>&1

%files
%defattr(-,root,root)
/quadstorvtl/src/export/
/quadstorvtl/src/common/
/quadstorvtl/src/target-mode/
/quadstorvtl/etc/iet/ietd.conf.sample
/quadstorvtl/etc/iet/initiators.allow.sample
/quadstorvtl/etc/iet/targets.allow.sample
/quadstorvtl/bin/builditf
/quadstorvtl/bin/qlainst
/quadstorvtl/bin/qlauninst

%clean
rm -rf $RPM_BUILD_ROOT/
