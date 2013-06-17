#!/bin/sh
set -x
version="2.2.6"
sh buildinit.sh debian6
sh debiancore.sh
mv debian.deb quadstor-vtl-core-$version-debian6-x86_64.deb
sh debianitf.sh
mv debian.deb quadstor-vtl-itf-$version-debian6-x86_64.deb
