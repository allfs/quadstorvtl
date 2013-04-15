#/bin/sh
rm -f /quadstor/etc/quadstor-vtl-itf-version
rmdir /quadstor/lib/modules > /dev/null 2>&1
rmdir /quadstor/lib > /dev/null 2>&1
rm -f /quadstor/sbin/ietd
rm -f /quadstor/bin/ietadm
rm -rf /quadstor/src/target-mode
