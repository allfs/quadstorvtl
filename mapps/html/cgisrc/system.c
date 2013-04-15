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

#include "cgimain.h"

char version[128];

char *
get_version(char *path)
{
	FILE *fp;
	char buf[128];
	int len;

	fp = fopen(path, "r");
	if (!fp)
		return "Unknown";

	memset(buf, 0, sizeof(buf));
	fgets(buf, sizeof(buf) - 1, fp);
	fclose(fp);
	len = strlen(buf);
	if (!len) {
		return "Unknown";
	}

	if (buf[len - 1] == '\n')
		buf[len - 1] = 0;

	strcpy(version, buf);
	return version;
}

int main()
{
	FILE *fp;
	char status[256];
	char hostname[256];
	char *cols[] = {"name", "value", NULL};


	memset(hostname, 0, sizeof(hostname));
	fp = popen("hostname", "r");
	if (fp) {
		fgets(hostname, sizeof(hostname), fp);
		hostname[strlen(hostname) - 1] = 0;
		pclose(fp);
	}

	status[0] = 0;
	tl_client_get_string(status, MSG_ID_SERVER_STATUS);

	cgi_print_header("System Information", NULL, 1);

	cgi_print_thdr("System Information");

	cgi_print_table_div("system-table");

	cgi_print_div_trailer();

	cgi_print_table_start("system-table", cols, 0);

	cgi_print_row_start();
	cgi_print_column("name", "System Name");
	cgi_print_comma();
	cgi_print_column("value", hostname);
	cgi_print_row_end();

	cgi_print_row_start();
	cgi_print_column("name", "Core Version");
	cgi_print_comma();
	cgi_print_column("value", get_version("/quadstor/etc/quadstor-vtl-core-version"));
	cgi_print_row_end();

	cgi_print_row_start();
	cgi_print_column("name", "Itf Version");
	cgi_print_comma();
	cgi_print_column("value", get_version("/quadstor/etc/quadstor-vtl-itf-version"));
	cgi_print_row_end();

	cgi_print_row_start();
	cgi_print_column("name", "Server Status");
	cgi_print_comma();
	cgi_print_column("value", cgi_strip_newline(status));
	cgi_print_row_end();


	cgi_print_table_end("system-table");

	cgi_print_body_trailer();
	return 0;
}
