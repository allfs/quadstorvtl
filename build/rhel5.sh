#!/bin/sh
set -x
sh buildinit.sh rhel5
rpmbuild -bb quadstorcore.spec && rpmbuild -bb quadstoritf.spec
