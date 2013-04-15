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

#ifndef QS_CGIMAIN_H_
#define QS_CGIMAIN_H_
#include <apicommon.h>
#include <html-lib.h>
#include <cgi-lib.h>
#include <tlclntapi.h>

void cgi_print_header(char *title, char *jsfile, int nocache);
void __cgi_print_header(char *title, char *jsfile, int nocache, char *meta, int refresh, char *onload);
void cgi_refresh(char *cgiscript, char *title, char *msg, int timeout);
void cgi_print_div_trailer(void);
void cgi_print_body_trailer(void);
void cgi_print_thdr(char *msg);
void cgi_print_table_start(char *table, char *cols[], int sortable);
void cgi_print_table_end(char *table);
void cgi_print_row_start(void);
void cgi_print_row_end(void);
void cgi_print_comma(void);
void cgi_print_column(char *name, char *value);
void cgi_print_table_div(char *table);
char * cgi_strip_newline(char *str);

void cgi_print_div_container(int width, char *align);
void cgi_print_div_padding(int pad);
void cgi_print_div_start(char *align);
void cgi_print_div_end(void);
void cgi_print_form_start_check(char *name, char *action, char *method, char *func);
void cgi_print_form_start(char *name, char *action, char *method, int checkform);
void cgi_print_form_end(void);
void cgi_print_paragraph(char *str);
void cgi_print_submit_button(char *name, char *value);
void cgi_print_text_input(char *name, int size, char *value, int maxlength);
void cgi_print_text_input_td(char *name, int size, char *value, int maxlength);
void cgi_print_checkbox_input(char *name, int checked);
void cgi_print_checkbox_input_td(char *name, int checked);
void cgi_print_error_page(char *msg);
void cgi_print_header_error_page(char *msg);
void cgi_print_break(void);
void cgi_redirect(char *cgiscript);

enum {
	CGI_DELETE_VCARTRIDGE,
	CGI_CLEAR_COMPLETED_JOBS,
};

#define cgi_print_column_format(nm,fmt,args...)	printf("%s: '"fmt"'", nm, ##args)

struct vtltype {
	int type;
	char *name;
};

struct voltype {
	int type;
	char *name;
};

struct drivetype {
	int type;
	char *name;
};

#endif
