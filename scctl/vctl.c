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

void
usage(void)
{
	printf("Usage: vtl -n <vtlname> -t <vtltype> -d <drivetype> -c <drivecount> -s <slots>\n"); 
}

int main(int argc, char *argv[]) {
	char name[40];
	int type = LIBRARY_TYPE_VADIC_SCALAR24;
	int ndrives = 0;
	int drivetype = DRIVE_TYPE_VHP_SDLT320;
	int slots = 50;
	int c;
	char tempfile[50];
	FILE *fp;
	int fd, i;
	int retval;
	int tl_id;
	char reply[256];

	name[0] = 0;

	while ((c = getopt(argc, argv, "n:t:d:c:i:s:" )) != -1) {
		switch (c) {
			case 'n':
				strcpy(name, optarg);
				break;
			case 't':
				type = atoi(optarg);
				break;
			case 'd':
				drivetype = atoi(optarg);
				break;
			case 'c':
				ndrives = atoi(optarg);
				break;
			case 's':
				slots = atoi(optarg);
				break;
			default:
				usage();
				exit(1);
		}
	}

	if (!ndrives)
	{
		usage();
		exit(1);
	}

	if (!name[0])
	{
		usage();
		exit(1);
	}

	strcpy(tempfile, "/tmp/.quadstortl.XXXXXX");
	fd = mkstemp(tempfile);
	if (fd == -1)
	{
		fprintf(stderr, "failed to create temp file\n");
		exit(1);
	}
	close(fd);

	fp = fopen(tempfile, "w");
	if (!fp)
	{
		fprintf(stderr, "failed to open temp file\n");
		remove(tempfile);
		exit(1);
	}

        fprintf (fp, "<vtlconf>\n");
        fprintf (fp, "name: %s\n", name);
        fprintf (fp, "slots: %d\n", slots);
        fprintf (fp, "type: %d\n", type);

	for (i = 0; i < ndrives; i++)
	{
		fprintf (fp, "<drive>\n");
		fprintf (fp, "name: drive%d\n", i);
		fprintf (fp, "type: %d\n", drivetype);
		fprintf (fp, "</drive>\n");
	}
	fprintf (fp, "</vtlconf\n");
	fclose(fp);
	reply[0] = 0;
	retval = tl_client_add_vtl_conf(tempfile, &tl_id, reply);
	remove(tempfile);

	if (retval != 0)
	{
		fprintf(stderr, "Failed to create VTL. Msg is %s\n", reply);
	}
	else
	{
		printf("Created VTL'%s' with id %d\n", name, tl_id+1);
	}
	return 0;
}
