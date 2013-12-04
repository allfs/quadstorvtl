#!/bin/sh

if [ -d /usr/local/www/apache22/ ]; then 
	htdocs=/usr/local/www/apache22/data;
	cgibin=/usr/local/www/apache22/cgi-bin;
elif [ -d /usr/local/www/apache24/ ]; then 
	htdocs=/usr/local/www/apache24/data;
	cgibin=/usr/local/www/apache24/cgi-bin;
elif [ -d /usr/local/www/ ]; then
	htdocs=/usr/local/www/data;
	cgibin=//usr/local/www/cgi-bin;
else
	htdocs=/var/www/html
	cgibin=/var/www/cgi-bin
fi

if [ -d /quadstorvtl/httpd/cgi-bin ]; then
	cgilist=`cd /quadstorvtl/httpd/cgi-bin && ls -1 *.cgi`
	for i in $cgilist; do
		rm -f $cgibin/$i
	done
fi

rm -rf $htdocs/quadstorvtl

if [ -f $htdocs/index.html ]; then
	cmp=`cmp -s $htdocs/index.html $htdocs/vtindex.html`
	if [ "$?" = "0" ]; then
		rm -f $htdocs/index.html
	fi
fi
rm -f $htdocs/vtindex.html

cmod=`/sbin/kldstat | grep vtlcore`
if [ "$cmod" = "" ]; then
	return
fi

/etc/rc.d/quadstorvtl stop > /dev/null 2>&1
cmod=`/sbin/kldstat | grep vtlcore`
if [ "$cmod" = "" ]; then
	return
fi

/etc/rc.d/quadstorvtl onestop > /dev/null 2>&1
cmod=`/sbin/kldstat | grep vtlcore`
if [ "$cmod" = "" ]; then
	return
fi

echo "Unable to shutdown QUADStor service cleanly. Please restart the system and try again"
exit 1
