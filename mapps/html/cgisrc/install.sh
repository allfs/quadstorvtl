#!/bin/sh

rm -rf $QUADSTOR_INSTALL_ROOT/httpd/
mkdir -p $QUADSTOR_INSTALL_ROOT/httpd/www/quadstorvtl
mkdir -p $QUADSTOR_INSTALL_ROOT/httpd/cgi-bin/
if [ ! -d $QUADSTOR_INSTALL_ROOT/httpd/www/quadstorvtl/yui ]; then
	cp -r yui $QUADSTOR_INSTALL_ROOT/httpd/www/quadstorvtl/
fi

cp -f *.png $QUADSTOR_INSTALL_ROOT/httpd/www/quadstorvtl
cp -f *.js $QUADSTOR_INSTALL_ROOT/httpd/www/quadstorvtl
cp -f *.css $QUADSTOR_INSTALL_ROOT/httpd/www/quadstorvtl
cp -f vtindex.html $QUADSTOR_INSTALL_ROOT/httpd/www/
for i in `ls -1 *.cgi`;do
	echo "cp -f $i $QUADSTOR_INSTALL_ROOT/httpd/cgi-bin/"; \
	cp -f $i $QUADSTOR_INSTALL_ROOT/httpd/cgi-bin/; \
done
if [ "$1" = "localinstall" ]; then
	exit 0
fi

if [ -d /usr/local/www/apache22/ ]; then 
	htdocs=/usr/local/www/apache22/data;
	cgibin=/usr/local/www/apache22/cgi-bin;
elif [ -d /usr/local/www/ ]; then
	htdocs=/usr/local/www/data;
	cgibin=//usr/local/www/cgi-bin;
elif [ -f /etc/debian_version ]; then
	htdocs="/var/www"
	cgibin="/usr/lib/cgi-bin"
elif [ -f /etc/SuSE-release ]; then
	htdocs="/srv/www/htdocs"
	cgibin="/srv/www/cgi-bin"
elif [ -f /etc/redhat-release ]; then
	htdocs=/var/www/html
	cgibin=/var/www/cgi-bin
else
	htdocs="/var/www"
	cgibin="/usr/lib/cgi-bin"
fi

sudo mkdir -p $htdocs/quadstorvtl
if [ ! -d $htdocs/quadstorvtl/yui ]; then
	sudo cp -r yui $htdocs/quadstorvtl/
fi

sudo cp -f *.js $htdocs/quadstorvtl
sudo cp -f *.css $htdocs/quadstorvtl
sudo cp -f *.png $htdocs/quadstorvtl
sudo cp -f vtindex.html $htdocs/
for i in `ls -1 *.cgi`;do
	echo "cp -f $i $cgibin"; \
	sudo cp -f $i $cgibin; \
done

