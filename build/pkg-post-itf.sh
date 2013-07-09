#!/bin/sh
if [ ! -f /quadstor/etc/iet/targets.allow ]; then
	cp /quadstor/etc/iet/targets.allow.sample /quadstor/etc/iet/targets.allow
fi

if [ ! -f /quadstor/etc/iet/initiators.allow ]; then
	cp /quadstor/etc/iet/initiators.allow.sample /quadstor/etc/iet/initiators.allow
fi

if [ ! -f /quadstor/etc/iet/ietd.conf ]; then
	cp /quadstor/etc/iet/ietd.conf.sample /quadstor/etc/iet/ietd.conf
fi

echo "2.2.8 for FreeBSD 9.0" > /quadstor/etc/quadstor-vtl-itf-version
exit 0
