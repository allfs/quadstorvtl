#!/bin/sh
if [ ! -d /quadstor/pgsql/data ]; then
	/quadstor/pgsql/scripts/pgpost.sh > /dev/null 2>&1
fi

/quadstor/pgsql/scripts/pgpatch.sh > /dev/null 2>&1

cp -f /quadstor/etc/quadstor /etc/rc.d/
chmod +x /quadstor/etc/quadstor
chmod +x /quadstor/lib/modules/*

cp -f /quadstor/lib/modules/ispmod.ko /boot/kernel/
chmod +x /quadstor/lib/modules/ispmod.ko

if [ -d /usr/local/www/apache22/ ]; then 
	htdocs=/usr/local/www/apache22/data;
	cgibin=/usr/local/www/apache22/cgi-bin;
elif [ -d /usr/local/www/ ]; then
	htdocs=/usr/local/www/data;
	cgibin=//usr/local/www/cgi-bin;
else
	htdocs=/var/www/html
	cgibin=/var/www/cgi-bin
fi

cp -f /quadstor/httpd/www/*.html $htdocs/
rm -rf $htdocs/quadstor/
mkdir -p $htdocs/quadstor
cp -fr /quadstor/httpd/www/quadstor/* $htdocs/quadstor/
cp -f /quadstor/httpd/cgi-bin/* $cgibin/
mkdir -p /quadstor/etc
echo "2.2.7 for FreeBSD 9.0" > /quadstor/etc/quadstor-vtl-core-version

exit 0
