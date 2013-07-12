set -e
os=`uname`

if [ "$1" = "install" ]; then
	mkdir -p /quadstorvtl/lib/modules
	cp -f iscsit.ko /quadstorvtl/lib/modules/
	exit 0
fi

if [ "$os" = "FreeBSD" ]; then
	make -f Makefile.iet $1
else
	make $1
fi
