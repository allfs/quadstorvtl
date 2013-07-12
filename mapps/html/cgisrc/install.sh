#!/bin/sh

rm -rf /quadstorvtl/httpd/
mkdir -p /quadstorvtl/httpd/www/quadstor
mkdir -p /quadstorvtl/httpd/cgi-bin/
cp -r yui /quadstorvtl/httpd/www/quadstor/
cp -f *.png /quadstorvtl/httpd/www/quadstor/
cp -f *.js /quadstorvtl/httpd/www//quadstor/
cp -f *.css /quadstorvtl/httpd/www//quadstor/
cp -f index.html /quadstorvtl/httpd/www/

for i in `ls -1 *.cgi`;do
	echo "cp -f $i /quadstorvtl/httpd/cgi-bin/"; \
	cp -f $i /quadstorvtl/httpd/cgi-bin/; \
done

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

mkdir -p $htdocs/quadstorvtl/ 
if [ ! -d $htdocs/quadstorvtl/yui ]; then
	cp -r yui $htdocs/quadstorvtl/
fi

cp -f *.js $htdocs/quadstorvtl/
cp -f *.css $htdocs/quadstorvtl/
cp -f *.png $htdocs/quadstorvtl/
cp -f index.html $htdocs/
for i in `ls -1 *.cgi`;do
	echo "cp -f $i $cgibin"; \
	cp -f $i $cgibin; \
done
