#!/bin/bash
set -x 
./configure --without-docdir --with-pgport=9988 --without-tcl --without-perl --without-python --without-krb5 --without-pam --without-bonjour --without-openssl --without-ldap --prefix=/quadstorvtl/pgsql --enable-thread-safety --without-readline --without-zlib
