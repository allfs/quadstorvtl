#!/bin/sh
set -x
sh buildinit.sh sles11
rpmbuild -bb quadstorcoresles.spec && rpmbuild -bb quadstoritfsles.spec
