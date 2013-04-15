#!/bin/sh
set -x 
for i in `ls -1`; do
	sed -i -e "s/3.0.26/2.2.0/g" $i
	sed -i -e "s/3.0.26-x86_64/2.2.0-x86_64/g" $i
done
