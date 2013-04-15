#!/bin/sh
set -x
curdir=`pwd`
sh buildinit.sh bsd9
cd /quadstor/quadstor/pgsql && gmake install
cd $curdir
rm -f createpkg9.sh createitf9.sh
cp createpkg.sh createpkg9.sh
cp createitf.sh createitf9.sh

sed -i -e "s/FreeBSD8.2/FreeBSD9.0/" createpkg9.sh
sed -i -e "s/FreeBSD8.2/FreeBSD9.0/" createitf9.sh
sed -i -e "s/FreeBSD 8.2/FreeBSD 9.0/" pkg-post-itf.sh
sed -i -e "s/FreeBSD 8.2/FreeBSD 9.0/" pkg-post.sh

sh createpkg9.sh && sh createitf9.sh
