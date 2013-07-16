#!/bin/sh

if [ -d /quadstor/pgsql/data -a ! -d /quadstorvtl/pgsql/data ]; then
	vtl=`grep -r vtl /quadstor/pgsql/data/base/* 2> /dev/null`
	tdisk=`grep -r tdisk /quadstor/pgsql/data/base/* 2> /dev/null`
	if [ "$vtl" != "" -a "$tdisk" = "" ]; then
		echo "WARNING: Moving /quadstor/pgsql/data to /quadstorvtl/pgsql/"
		echo "vtdbuser" | /usr/sbin/pw add user vtdbuser -d /quadstorvtl/pgsql -h 0 
		mv -f /quadstor/pgsql/data /quadstorvtl/pgsql/data
		chown -R vtdbuser:vtdbuser /quadstorvtl/pgsql/data
	fi
fi

if [ ! -d /quadstorvtl/pgsql/data ]; then
	/quadstorvtl/pgsql/scripts/pgpost.sh > /dev/null 2>&1
fi

/quadstorvtl/pgsql/scripts/pgpatch.sh > /dev/null 2>&1

cp -f /quadstorvtl/etc/quadstorvtl /etc/rc.d/quadstorvtl
chmod +x /quadstorvtl/etc/quadstorvtl
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
cp -fr /quadstorvtl/httpd/www/quadstorvtl/* $htdocs/quadstorvtl/
cp -f /quadstorvtl/httpd/cgi-bin/* $cgibin/
mkdir -p /quadstorvtl/etc
echo "2.2.8 for FreeBSD 9.0" > /quadstorvtl/etc/quadstor-vtl-core-version

core=`pkg_info | grep quadstor-core`
if [ "$core" = "" ]; then
	cp -f $htdocs/vtindex.html $htdocs/index.html
fi

exit 0
