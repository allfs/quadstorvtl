#!/bin/sh
set -x
curdir=`pwd`
sh buildinit.sh bsd
cd /quadstorvtl/quadstor/pgsql && gmake install
cd $curdir
sh createpkg.sh && sh createitf.sh
