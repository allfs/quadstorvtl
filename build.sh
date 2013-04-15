set -x
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

rm -f /quadstor/lib/modules/corelib.o
rm -f /quadstor/quadstor/export/corelib.o
cd /quadstor/quadstor/core && sh build.sh clean && sh build.sh $clean
checkerror

if [ "$clean" != install ]; then
if [ "$os" = "FreeBSD" ]; then
	cd /quadstor/quadstor/export && make -f Makefile.core $clean
	cd /quadstor/quadstor/export && make -f Makefile.ldev $clean
	checkerror
else
	cd /quadstor/quadstor/export && make $clean
	checkerror
fi
fi

cd /quadstor/quadstor/target-mode/iscsi/kernel && $GMAKE -f Makefile.kmod $clean
checkerror

cd /quadstor/quadstor/target-mode/fc/ && $GMAKE $clean
checkerror

cd /quadstor/quadstor/others/ && $GMAKE $clean
checkerror
cd /quadstor/quadstor/library && $GMAKE $clean
checkerror
cd /quadstor/quadstor/target-mode/iscsi/usr && $GMAKE $clean
checkerror
cd /quadstor/quadstor/mapps/html && $GMAKE $clean
checkerror
cd /quadstor/quadstor/masterd && $GMAKE $clean
checkerror
cd /quadstor/quadstor/scctl && $GMAKE $clean
checkerror

cd /quadstor/quadstor/etc && $GMAKE $clean
checkerror

if [ "$clean" = "" ]; then
	exit 0
fi

rm -f /quadstor/quadstor/core/@
rm -f /quadstor/quadstor/core/x86
rm -f /quadstor/quadstor/core/machine
rm -f /quadstor/quadstor/export/@
rm -f /quadstor/quadstor/export/x86
rm -f /quadstor/quadstor/export/machine
rm -f /quadstor/quadstor/target-mode/iscsi/kernel/@
rm -f /quadstor/quadstor/target-mode/iscsi/kernel/x86
rm -f /quadstor/quadstor/target-mode/iscsi/kernel/machine
rm -f /quadstor/quadstor/target-mode/fc/isp/@
rm -f /quadstor/quadstor/target-mode/fc/isp/x86
rm -f /quadstor/quadstor/target-mode/fc/isp/machine
exit 0
