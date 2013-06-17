#/bin/sh
set -x
curdir=`pwd`
libvers="2.2.6"

cd /quadstor/quadstor/ && sh build.sh clean
cd /quadstor/quadstor/ && sh build.sh
cd $curdir

DEBIAN_ROOT=$curdir/debian
rm -rf $DEBIAN_ROOT
mkdir $DEBIAN_ROOT
mkdir $DEBIAN_ROOT/DEBIAN
cp debian-core-control $DEBIAN_ROOT/DEBIAN/control
install -m 755 debian-core-prerm $DEBIAN_ROOT/DEBIAN/prerm
install -m 755 debian-core-postrm $DEBIAN_ROOT/DEBIAN/postrm
install -m 755 debian-core-preinst $DEBIAN_ROOT/DEBIAN/preinst
install -m 755 debian-core-postinst $DEBIAN_ROOT/DEBIAN/postinst

install -m 755 -d $DEBIAN_ROOT/quadstor/lib/modules
install -m 755 -d $DEBIAN_ROOT/quadstor/bin
install -m 755 -d $DEBIAN_ROOT/quadstor/sbin
install -m 755 -d $DEBIAN_ROOT/quadstor/lib
install -m 755 -d $DEBIAN_ROOT/quadstor/lib/modules
install -m 755 -d $DEBIAN_ROOT/quadstor/etc
install -m 755 -d $DEBIAN_ROOT/quadstor/etc/iet
install -m 755 -d $DEBIAN_ROOT/usr/lib/cgi-bin
install -m 755 -d $DEBIAN_ROOT/var/www/
install -m 755 -d $DEBIAN_ROOT/var/www/quadstor
install -m 755 -d $DEBIAN_ROOT/etc/init.d

cd /quadstor/quadstor/pgsql
make install DESTDIR=$DEBIAN_ROOT
rm -rf $DEBIAN_ROOT/quadstor/pgsql/data/

install -m 755 /quadstor/quadstor/masterd/mdaemon $DEBIAN_ROOT/quadstor/sbin/mdaemon
install -m 644  /quadstor/quadstor/library/client/libtlclnt.so $DEBIAN_ROOT/quadstor/lib/libtlclnt.so.$libvers
install -m 644 /quadstor/quadstor/library/server/libtlsrv.so $DEBIAN_ROOT/quadstor/lib/libtlsrv.so.$libvers
install -m 644 /quadstor/quadstor/library/common/libtlmsg.so $DEBIAN_ROOT/quadstor/lib/libtlmsg.so.$libvers
install -m 644 /quadstor/lib/modules/corelib.o $DEBIAN_ROOT/quadstor/lib/modules/
install -m 644 /quadstor/quadstor/mapps/html/cgisrc/index.html $DEBIAN_ROOT/var/www/
install -m 755 /quadstor/quadstor/mapps/html/cgisrc/*.cgi $DEBIAN_ROOT/usr/lib/cgi-bin/
install -m 755 /quadstor/quadstor/mapps/html/cgisrc/*.css $DEBIAN_ROOT/var/www/quadstor/
install -m 755 /quadstor/quadstor/mapps/html/cgisrc/*.js $DEBIAN_ROOT/var/www/quadstor/
install -m 755 /quadstor/quadstor/mapps/html/cgisrc/*.png $DEBIAN_ROOT/var/www/quadstor/
cp -pr /quadstor/quadstor/mapps/html/cgisrc/yui/ $DEBIAN_ROOT/var/www/quadstor/
install -m 744 /quadstor/quadstor/scctl/scctl $DEBIAN_ROOT/quadstor/bin/scctl
install -m 744 /quadstor/quadstor/scctl/fcconfig $DEBIAN_ROOT/quadstor/bin/fcconfig
install -m 744 /quadstor/quadstor/scctl/dbrecover $DEBIAN_ROOT/quadstor/bin/dbrecover
install -m 744 /quadstor/quadstor/etc/quadstor.linux $DEBIAN_ROOT/etc/init.d/quadstor
sed -i -e "s/Default-Start.*/Default-Start:\t\t2 3 4 5/g" $DEBIAN_ROOT/etc/init.d/quadstor
sed -i -e "s/Default-Stop.*/Default-Stop:\t\t\t0 1 6/g" $DEBIAN_ROOT/etc/init.d/quadstor
install -m 444 /quadstor/quadstor/build/LICENSE $DEBIAN_ROOT/quadstor/
install -m 444 /quadstor/quadstor/build/GPLv2 $DEBIAN_ROOT/quadstor/

objcopy --strip-unneeded $DEBIAN_ROOT/quadstor/bin/scctl
objcopy --strip-unneeded $DEBIAN_ROOT/quadstor/bin/dbrecover

#Install src
install -m 755 -d $DEBIAN_ROOT/quadstor/src/others
install -m 644 /quadstor/quadstor/library/server/md5*.[ch] $DEBIAN_ROOT/quadstor/src/others/
install -m 644 /quadstor/quadstor/core/lzf*.[ch] $DEBIAN_ROOT/quadstor/src/others/
install -m 644 /quadstor/quadstor/core/lz4*.[ch] $DEBIAN_ROOT/quadstor/src/others/
install -m 644 /quadstor/quadstor/core/sysdefs/*.h $DEBIAN_ROOT/quadstor/src/others/


cd $DEBIAN_ROOT/quadstor/lib && ln -fs libtlclnt.so.$libvers libtlclnt.so.1
cd $DEBIAN_ROOT/quadstor/lib && ln -fs libtlclnt.so.$libvers libtlclnt.so
cd $DEBIAN_ROOT/quadstor/lib && ln -fs libtlsrv.so.$libvers libtlsrv.so.1
cd $DEBIAN_ROOT/quadstor/lib && ln -fs libtlsrv.so.$libvers libtlsrv.so
cd $DEBIAN_ROOT/quadstor/lib && ln -fs libtlmsg.so.$libvers libtlmsg.so.1
cd $DEBIAN_ROOT/quadstor/lib && ln -fs libtlmsg.so.$libvers libtlmsg.so
rm -f debian.deb
fakeroot dpkg-deb --build $DEBIAN_ROOT 

