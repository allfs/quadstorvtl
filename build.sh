set -x
buildroot=`pwd`
export QUADSTOR_ROOT="$buildroot"
os=`uname`
GMAKE="make"
if [ "$os" = "FreeBSD" ]; then
	GMAKE="gmake"
fi

checkerror() {
	if [ "$?" != "0" ]; then
		exit 1
	fi
}

clean=$1
if [ "$1" = "clobber" ]; then
	clean="clean"
fi

rm -f /quadstorvtl/lib/modules/corelib.o
rm -f /quadstorvtl/quadstor/export/corelib.o
cd /quadstorvtl/quadstor/core && sh build.sh clean && sh build.sh $clean
checkerror

if [ "$clean" = "x86" ]; then
	clean=""
fi

if [ "$clean" != install ]; then
if [ "$os" = "FreeBSD" ]; then
	cd /quadstorvtl/quadstor/export && make -f Makefile.core $clean
	cd /quadstorvtl/quadstor/export && make -f Makefile.ldev $clean
	checkerror
else
	cd /quadstorvtl/quadstor/export && make $clean
	checkerror
fi
fi

cd /quadstorvtl/quadstor/target-mode/iscsi/kernel && $GMAKE -f Makefile.kmod $clean
checkerror

cd /quadstorvtl/quadstor/target-mode/fc/ && $GMAKE $clean
checkerror

cd /quadstorvtl/quadstor/others/ && $GMAKE $clean
checkerror
cd /quadstorvtl/quadstor/library && $GMAKE $clean
checkerror
cd /quadstorvtl/quadstor/target-mode/iscsi/usr && $GMAKE $clean
checkerror
cd /quadstorvtl/quadstor/mapps/html && $GMAKE $clean
checkerror
cd /quadstorvtl/quadstor/masterd && $GMAKE $clean
checkerror
cd /quadstorvtl/quadstor/scctl && $GMAKE $clean
checkerror

cd /quadstorvtl/quadstor/etc && $GMAKE $clean
checkerror

if [ "$clean" = "" ]; then
	exit 0
fi

rm -f /quadstorvtl/quadstor/core/@
rm -f /quadstorvtl/quadstor/core/x86
rm -f /quadstorvtl/quadstor/core/machine
rm -f /quadstorvtl/quadstor/export/@
rm -f /quadstorvtl/quadstor/export/x86
rm -f /quadstorvtl/quadstor/export/machine
rm -f /quadstorvtl/quadstor/target-mode/iscsi/kernel/@
rm -f /quadstorvtl/quadstor/target-mode/iscsi/kernel/x86
rm -f /quadstorvtl/quadstor/target-mode/iscsi/kernel/machine
rm -f /quadstorvtl/quadstor/target-mode/fc/isp/@
rm -f /quadstorvtl/quadstor/target-mode/fc/isp/x86
rm -f /quadstorvtl/quadstor/target-mode/fc/isp/machine
exit 0
