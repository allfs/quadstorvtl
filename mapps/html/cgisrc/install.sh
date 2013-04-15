#!/bin/sh

rm -rf /quadstor/httpd/
mkdir -p /quadstor/httpd/www/quadstor
mkdir -p /quadstor/httpd/cgi-bin/
cp -r yui /quadstor/httpd/www/quadstor/
cp -f *.png /quadstor/httpd/www/quadstor/
cp -f *.js /quadstor/httpd/www//quadstor/
cp -f *.css /quadstor/httpd/www//quadstor/
cp -f index.html /quadstor/httpd/www/

for i in `ls -1 *.cgi`;do
	echo "cp -f $i /quadstor/httpd/cgi-bin/"; \
	cp -f $i /quadstor/httpd/cgi-bin/; \
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

mkdir -p $htdocs/quadstor/ 
if [ ! -d $htdocs/quadstor/yui ]; then
	cp -r yui $htdocs/quadstor/
fi

cp -f *.js $htdocs/quadstor/
cp -f *.css $htdocs/quadstor/
cp -f *.png $htdocs/quadstor/
cp -f index.html $htdocs/
for i in `ls -1 *.cgi`;do
	echo "cp -f $i $cgibin"; \
	cp -f $i $cgibin; \
done
