#!/bin/sh
if [ ! -f /quadstorvtl/etc/iet/targets.allow ]; then
	cp /quadstorvtl/etc/iet/targets.allow.sample /quadstorvtl/etc/iet/targets.allow
fi

if [ ! -f /quadstorvtl/etc/iet/initiators.allow ]; then
	cp /quadstorvtl/etc/iet/initiators.allow.sample /quadstorvtl/etc/iet/initiators.allow
fi

if [ ! -f /quadstorvtl/etc/iet/ietd.conf ]; then
	cp /quadstorvtl/etc/iet/ietd.conf.sample /quadstorvtl/etc/iet/ietd.conf
fi

echo "2.2.9 for FreeBSD 9.0" > /quadstorvtl/etc/quadstor-vtl-itf-version
exit 0
