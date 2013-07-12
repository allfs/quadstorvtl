#!/bin/sh

if [ -d /quadstor/pgsql/data ]; then
	vtl=`grep -r vtl /quadstor/pgsql/data/base/*`
	if [ "$vtl" != "" ]; then
		echo "WARNING: Moving /quadstor/pgsql/data to new /quadstorvtl/pgsql/ path"
		mv -f /quadstor/pgsql/data /quadstorvtl/pgsql/data
	fi
fi

if [ ! -d /quadstorvtl/pgsql/data ]; then
	/quadstorvtl/pgsql/scripts/pgpost.sh > /dev/null 2>&1
fi

/quadstorvtl/pgsql/scripts/pgpatch.sh > /dev/null 2>&1

cp -f /quadstorvtl/etc/quadstor /etc/rc.d/
chmod +x /quadstorvtl/etc/quadstor
chmod +x /quadstorvtl/lib/modules/*

cp -f /quadstorvtl/lib/modules/ispmod.ko /boot/kernel/
chmod +x /quadstorvtl/lib/modules/ispmod.ko

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

cp -f /quadstorvtl/httpd/www/*.html $htdocs/
rm -rf $htdocs/quadstorvtl/
mkdir -p $htdocs/quadstorvtl
cp -fr /quadstorvtl/httpd/www/quadstor/* $htdocs/quadstorvtl/
cp -f /quadstorvtl/httpd/cgi-bin/* $cgibin/
mkdir -p /quadstorvtl/etc
echo "2.2.8 for FreeBSD 9.0" > /quadstorvtl/etc/quadstor-vtl-core-version

exit 0
