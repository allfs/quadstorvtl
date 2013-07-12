#/bin/sh
rm -f /quadstorvtl/etc/quadstor-vtl-itf-version
rmdir /quadstorvtl/lib/modules > /dev/null 2>&1
rmdir /quadstorvtl/lib > /dev/null 2>&1
rm -f /quadstorvtl/sbin/ietd
rm -f /quadstorvtl/bin/ietadm
rm -rf /quadstorvtl/src/target-mode
