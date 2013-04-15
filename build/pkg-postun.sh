#/bin/sh
rm -f /quadstor/etc/quadstor-vtl-core-version
rmdir /quadstor/lib/modules > /dev/null 2>&1
rmdir /quadstor/lib > /dev/null 2>&1
rm -rf /quadstor/share/
rm -rf /quadstor/httpd/
rm -rf /quadstor/src/others
rm -rf /quadstor/src/export
rm -rf /quadstor/src/common
rm -rf /quadstor/pgsql/bin
rm -rf /quadstor/pgsql/etc
rm -rf /quadstor/pgsql/lib
rm -rf /quadstor/pgsql/scripts
rm -rf /quadstor/pgsql/share
rm -f /boot/kernel/ispmod.ko
rm -f /etc/rc.d/quadstor
rmdir /quadstor/bin > /dev/null 2>&1
rmdir /quadstor/sbin > /dev/null 2>&1
rmdir /quadstor/src > /dev/null 2>&1
rmdir /quadstor/etc > /dev/null 2>&1
exit 0
