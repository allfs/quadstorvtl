#!/bin/sh
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
