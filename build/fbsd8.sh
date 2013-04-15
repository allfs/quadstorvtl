#!/bin/sh
set -x
curdir=`pwd`
sh buildinit.sh bsd
cd /quadstor/quadstor/pgsql && gmake install
cd $curdir
sh createpkg.sh && sh createitf.sh
