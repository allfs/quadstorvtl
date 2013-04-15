/* 
 * Copyright (C) Shivaram Upadhyayula <shivaram.u@quadstor.com>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * Version 2 as published by the Free Software Foundation
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, 
 * Boston, MA  02110-1301, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <tlclntapi.h>

static int
mdaemon_load_conf(void)
{
	return tl_client_load_conf();
}

static int
mdaemon_unload_conf(void)
{
	return tl_client_unload_conf();
}

static int
mdaemon_disk_check(void)
{
	return tl_client_disk_check();
}

int main(int argc, char *argv[])
{
	int c;
	int load = 0;
	int unload = 0;
	int retval = 0;
	int dcheck = 0;

	if (geteuid() != 0) {
		fprintf(stderr, "This program can only be run as root\n");
		exit(1);
	}

	while ((c = getopt(argc, argv, "luc")) != -1) {
		switch (c) {
			case 'l':
				load = 1;
				break;
			case 'u':
				unload = 1;
				break;
			case 'c':
				dcheck = 1;
				break;
			default:
				exit(1);
		}
	}

	if (load)
		retval = mdaemon_load_conf();
	else if (dcheck)
		retval = mdaemon_disk_check();
	else if (unload)
		retval = mdaemon_unload_conf();
	return retval;
}
