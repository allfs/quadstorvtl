set -e
os=`uname`

if [ "$1" = "install" ]; then
	mkdir -p /quadstor/lib/modules
	cp -f iscsit.ko /quadstor/lib/modules/
	exit 0
fi

if [ "$os" = "FreeBSD" ]; then
	make -f Makefile.iet $1
else
	make $1
fi
