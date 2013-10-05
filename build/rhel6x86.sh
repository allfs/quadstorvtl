#!/bin/sh
set -x
sh buildinit.sh rhel6x86
cp -f quadstorcore.spec quadstorcorerhel6x86.spec
cp -f quadstoritf.spec quadstoritfrhel6x86.spec
sed -i -e "s/Release: .*/Release: rhel6/g" quadstorcorerhel6x86.spec
sed -i -e "s/Release: .*/Release: rhel6/g" quadstoritfrhel6x86.spec
sed -i -e "s/CentOS 5/CentOS 6 x86/g" quadstorcorerhel6x86.spec
sed -i -e "s/CentOS 5/CentOS 6 x86/g" quadstoritfrhel6x86.spec
sed -i -e "s/sh build.sh$/sh build.sh x86/g" quadstorcorerhel6x86.spec
sed -i -e "s/()(64bit)//g" quadstorcorerhel6x86.spec
rpmbuild -bb quadstorcorerhel6x86.spec && rpmbuild -bb quadstoritfrhel6x86.spec
