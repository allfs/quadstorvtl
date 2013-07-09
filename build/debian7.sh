#!/bin/sh
set -x
version="2.2.8"
sh buildinit.sh debian7
sh debiancore.sh
mv debian.deb quadstor-vtl-core-$version-debian7-x86_64.deb
sh debianitf.sh
mv debian.deb quadstor-vtl-itf-$version-debian7-x86_64.deb
