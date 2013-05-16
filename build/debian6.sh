#!/bin/sh
set -x
version="2.2.1"
sh buildinit.sh debian6
sh debiancore.sh
mv debian.deb quadstor-vtl-core-$version-x86_64.deb
sh debianitf.sh
mv debian.deb quadstor-vtl-itf-$version-x86_64.deb
