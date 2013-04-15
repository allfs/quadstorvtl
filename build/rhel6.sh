#!/bin/sh
set -x
sh buildinit.sh rhel6
cp -f quadstorcore.spec quadstorcorerhel6.spec
cp -f quadstoritf.spec quadstoritfrhel6.spec
sed -i -e "s/Release: .*/Release: rhel6/g" quadstorcorerhel6.spec
sed -i -e "s/Release: .*/Release: rhel6/g" quadstoritfrhel6.spec
sed -i -e "s/CentOS 5/CentOS 6/g" quadstorcorerhel6.spec
sed -i -e "s/CentOS 5/CentOS 6/g" quadstoritfrhel6.spec
rpmbuild -bb quadstorcorerhel6.spec && rpmbuild -bb quadstoritfrhel6.spec
