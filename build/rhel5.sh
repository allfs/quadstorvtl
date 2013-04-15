#!/bin/sh
set -x
sh buildinit.sh
rpmbuild -bb quadstorcore.spec && rpmbuild -bb quadstoritf.spec
