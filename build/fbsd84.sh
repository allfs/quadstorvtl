#!/bin/sh
set -x
curdir=`pwd`
sh buildinit.sh bsd
cd /quadstorvtl/quadstor/pgsql && gmake install
cd $curdir
sed -i -e "s/FreeBSD8.2/FreeBSD8.4/" createpkg.sh
sed -i -e "s/FreeBSD8.2/FreeBSD8.4/" createitf.sh
sed -i -e "s/FreeBSD 8.2/FreeBSD 8.4/" pkg-post-itf.sh
sed -i -e "s/FreeBSD 8.2/FreeBSD 8.4/" pkg-post.sh
sh createpkg.sh && sh createitf.sh
