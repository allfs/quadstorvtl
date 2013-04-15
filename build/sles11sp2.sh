#!/bin/sh
set -x
sh buildinit.sh sles11sp2
cp -f quadstorcoresles.spec quadstorcoreslessp2.spec
cp -f quadstoritfsles.spec quadstoritfslessp2.spec
sed -i -e "s/Release: .*/Release: sles11sp2/g" quadstorcoreslessp2.spec
sed -i -e "s/Release: .*/Release: sles11sp2/g" quadstoritfslessp2.spec
sed -i -e "s/SP1/SP2/g" quadstorcoreslessp2.spec
sed -i -e "s/SP1/SP2/g" quadstoritfslessp2.spec
rpmbuild -bb quadstorcoreslessp2.spec && rpmbuild -bb quadstoritfslessp2.spec
