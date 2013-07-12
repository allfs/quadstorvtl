#/bin/sh
rm -f /quadstorvtl/etc/quadstor-vtl-core-version
rmdir /quadstorvtl/lib/modules > /dev/null 2>&1
rmdir /quadstorvtl/lib > /dev/null 2>&1
rm -rf /quadstorvtl/share/
rm -rf /quadstorvtl/httpd/
rm -rf /quadstorvtl/src/others
rm -rf /quadstorvtl/src/export
rm -rf /quadstorvtl/src/common
rm -rf /quadstorvtl/pgsql/bin
rm -rf /quadstorvtl/pgsql/etc
rm -rf /quadstorvtl/pgsql/lib
rm -rf /quadstorvtl/pgsql/scripts
rm -rf /quadstorvtl/pgsql/share
rm -f /boot/kernel/ispmod.ko
rm -f /etc/rc.d/quadstor
rmdir /quadstorvtl/bin > /dev/null 2>&1
rmdir /quadstorvtl/sbin > /dev/null 2>&1
rmdir /quadstorvtl/src > /dev/null 2>&1
rmdir /quadstorvtl/etc > /dev/null 2>&1
exit 0
