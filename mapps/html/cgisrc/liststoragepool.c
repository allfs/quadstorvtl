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

#include <html-lib.h>
#include "cgimain.h"

int main()
{
	int retval;
	struct group_list group_list;
	struct group_info *group_info;
	char name[512];
	char *cols[] = {"{ key: 'Name', sortable: true}", "Disks", "{ key: 'Details', allowHTML: true }", "WORM", "{ key: 'Delete', label: ' ',  allowHTML: true }", NULL};

	cgi_print_header("Storage Pools", NULL, 1);

	retval = tl_client_list_groups(&group_list, MSG_ID_LIST_GROUP);
	if (retval != 0)
		cgi_print_error_page("Getting pool list failed\n");

	cgi_print_thdr("Configured Pools");
	if (TAILQ_EMPTY(&group_list)) {
		cgi_print_div_start("center");
		cgi_print_paragraph("None");
		cgi_print_div_end();
	}
	else {
		cgi_print_table_div("pools-table");
	}

	cgi_print_div_start("center");
	cgi_print_form_start("addstoragepool", "addstoragepool.cgi", "post", 0);
	cgi_print_submit_button("submit", "Add Pool");
	cgi_print_form_end();
	cgi_print_div_end();

	cgi_print_div_trailer();

	if (TAILQ_EMPTY(&group_list))
		goto skip;

	cgi_print_table_start("pools-table", cols, 1);

	TAILQ_FOREACH(group_info, &group_list, q_entry) {
		cgi_print_row_start();

#if 0
		cgi_print_column_format("ID", "%u", group_info->group_id);
		cgi_print_comma();
#endif

		memset(name, 0, sizeof(name));
		strcpy(name, group_info->name);
		cgi_print_column("Name", name);
		cgi_print_comma();

		cgi_print_column_format("Disks", "%d", group_info->disks);
		cgi_print_comma();

		if (group_info->worm)
			cgi_print_column("WORM", "Yes");
		else
			cgi_print_column("WORM", "No");
		cgi_print_comma();
			
		cgi_print_column_format("Details", "<a href=\"viewstoragepool.cgi?group_id=%u&name=%s\">View</a>", group_info->group_id, group_info->name);
		cgi_print_comma();

		if (group_info->group_id)
			cgi_print_column_format("Delete", "<a href=\"deletestoragepool.cgi?group_id=%u\"  onclick=\\'return confirm(\\\"Delete pool %s?\\\");\\'><img src=\"/quadstor/delete.png\" width=16px height=16px border=0></a>", group_info->group_id, group_info->name);
		else
			cgi_print_column("Delete", " ");
		cgi_print_row_end();
	}

	group_list_free(&group_list);

	cgi_print_table_end("pools-table");
skip:
	cgi_print_body_trailer();
	return 0;
}
