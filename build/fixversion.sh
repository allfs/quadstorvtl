#!/bin/sh
set -x 
for i in `ls -1`; do
	sed -i -e "s/2.2.16/2.2.16/g" $i
	sed -i -e "s/2.2.16-x86_64/2.2.16-x86_64/g" $i
done
