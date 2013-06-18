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

struct vtltype vtltypes[] = {
	{LIBRARY_TYPE_VADIC_SCALAR24, LIBRARY_NAME_VADIC_SCALAR24},
	{LIBRARY_TYPE_VADIC_SCALAR100, LIBRARY_NAME_VADIC_SCALAR100},
	{LIBRARY_TYPE_VADIC_SCALARi2000, LIBRARY_NAME_VADIC_SCALARi2000},
	{LIBRARY_TYPE_VHP_ESL9000, LIBRARY_NAME_VHP_ESL9000},
	{LIBRARY_TYPE_VHP_ESLSERIES, LIBRARY_NAME_VHP_ESLSERIES},
	{LIBRARY_TYPE_VHP_EMLSERIES, LIBRARY_NAME_VHP_EMLSERIES},
	{LIBRARY_TYPE_VIBM_3583, LIBRARY_NAME_VIBM_3583},
	{LIBRARY_TYPE_VIBM_3584, LIBRARY_NAME_VIBM_3584},
	{LIBRARY_TYPE_VIBM_TS3100, LIBRARY_NAME_VIBM_TS3100},
	{LIBRARY_TYPE_VHP_MSLSERIES, LIBRARY_NAME_VHP_MSLSERIES},
	{LIBRARY_TYPE_VHP_MSL6000, LIBRARY_NAME_VHP_MSL6000},
	{LIBRARY_TYPE_VOVL_NEOSERIES, LIBRARY_NAME_VOVL_NEOSERIES},
};

struct drivetype drivetypes[] = {
	{DRIVE_TYPE_VHP_DLTVS80, DRIVE_NAME_VHP_DLTVS80},
	{DRIVE_TYPE_VHP_DLTVS160, DRIVE_NAME_VHP_DLTVS160},
	{DRIVE_TYPE_VHP_SDLT220, DRIVE_NAME_VHP_SDLT220},
	{DRIVE_TYPE_VHP_SDLT320, DRIVE_NAME_VHP_SDLT320},
	{DRIVE_TYPE_VHP_SDLT600, DRIVE_NAME_VHP_SDLT600},
	{DRIVE_TYPE_VQUANTUM_SDLT220, DRIVE_NAME_VQUANTUM_SDLT220},
	{DRIVE_TYPE_VQUANTUM_SDLT320, DRIVE_NAME_VQUANTUM_SDLT320},
	{DRIVE_TYPE_VQUANTUM_SDLT600, DRIVE_NAME_VQUANTUM_SDLT600},
	{DRIVE_TYPE_VHP_ULT232, DRIVE_NAME_VHP_ULT232},
	{DRIVE_TYPE_VHP_ULT448, DRIVE_NAME_VHP_ULT448},
	{DRIVE_TYPE_VHP_ULT460, DRIVE_NAME_VHP_ULT460},
	{DRIVE_TYPE_VHP_ULT960, DRIVE_NAME_VHP_ULT960},
	{DRIVE_TYPE_VHP_ULT1840, DRIVE_NAME_VHP_ULT1840},
	{DRIVE_TYPE_VHP_ULT3280, DRIVE_NAME_VHP_ULT3280},
	{DRIVE_TYPE_VHP_ULT6250, DRIVE_NAME_VHP_ULT6250},
	{DRIVE_TYPE_VIBM_3580ULT1, DRIVE_NAME_VIBM_3580ULT1},
	{DRIVE_TYPE_VIBM_3580ULT2, DRIVE_NAME_VIBM_3580ULT2},
	{DRIVE_TYPE_VIBM_3580ULT3, DRIVE_NAME_VIBM_3580ULT3},
	{DRIVE_TYPE_VIBM_3580ULT4, DRIVE_NAME_VIBM_3580ULT4},
	{DRIVE_TYPE_VIBM_3580ULT5, DRIVE_NAME_VIBM_3580ULT5},
	{DRIVE_TYPE_VIBM_3580ULT6, DRIVE_NAME_VIBM_3580ULT6},
};

struct voltype voltypes[] = {
	{VOL_TYPE_CLEANING, VOL_NAME_CLEANING},
	{VOL_TYPE_DIAGNOSTICS, VOL_NAME_DIAG},
	{VOL_TYPE_DLT_4, VOL_NAME_DLT_4},
	{VOL_TYPE_VSTAPE, VOL_NAME_VSTAPE},
	{VOL_TYPE_SDLT_1, VOL_NAME_SDLT_1},
	{VOL_TYPE_SDLT_2, VOL_NAME_SDLT_2},
	{VOL_TYPE_SDLT_3, VOL_NAME_SDLT_3},
	{VOL_TYPE_LTO_1, VOL_NAME_LTO_1},
	{VOL_TYPE_LTO_2, VOL_NAME_LTO_2},
	{VOL_TYPE_LTO_3, VOL_NAME_LTO_3},
	{VOL_TYPE_LTO_4, VOL_NAME_LTO_4},
	{VOL_TYPE_LTO_5, VOL_NAME_LTO_5},
	{VOL_TYPE_LTO_6, VOL_NAME_LTO_6},
};

int num_voltypes = (sizeof(voltypes) / sizeof(struct voltype));

void
__cgi_print_header(char *title, char *jsfile, int nocache, char *meta, int refresh, char *onload)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	html_header();
	printf("<!doctype html>\n");
        printf("<html>\n");
	printf("<head>\n");
	printf("<meta http-equiv=\"content-type\" content=\"text/html;charset=UTF-8\" />\n");
	if (meta)
		printf("%s", meta);

#if 0
	if (nocache) {
		printf("<meta http-equiv=\"cache-control\" content=\"no-cache\" />\n");
		printf("<meta http-equiv=\"pragma\" content=\"no-cache\" />\n");
		printf("<meta http-equiv=\"expires\" content=\"-1\" />\n");
	}
#endif

	if (refresh)
		printf("<meta http-equiv=\"refresh\" content=\"%d\" />\n", refresh);
	printf("<title>%s</title>\n", title);
	printf("<link rel=\"stylesheet\" type=\"text/css\" href=\"/quadstor/yui/build/cssgrids/grids-min.css\" />\n");
	printf("<link rel=\"stylesheet\" type=\"text/css\" href=\"/quadstor/yui/build/cssfonts/fonts-min.css\" />\n");
	printf("<link rel=\"stylesheet\" type=\"text/css\" href=\"/quadstor/yui/build/cssbutton/cssbutton.css\" />\n");
	printf("<link rel=\"stylesheet\" type=\"text/css\" href=\"/quadstor/quadstor.css\" />\n");
	printf("<script type=\"text/javascript\" src=\"/quadstor/yui/build/yui/yui-min.js\"></script>\n");
	if (jsfile)
		printf("<script type=\"text/javascript\" src=\"/quadstor/%s\"></script>\n", jsfile);
	printf("</head>\n");
	if (onload)
		printf("<body class=\"yui3-skin-sam\" onload=\"%s\">\n", onload);
	else
		printf("<body class=\"yui3-skin-sam\">\n");
	printf("<img src=\"/quadstor/quadstorlogo.png\" alt=\"QUADStor Logo\"/>\n");
	printf("<div class=\"yui3-g\">\n");
	printf("<div class=\"yui3-u-23-24\" id=\"main\">\n");
	printf("\n");

	/* Start of menus */
	printf("<div id=\"quadstormenu\" class=\"yui3-menu yui3-menu-horizontal\">\n");
	printf("<div class=\"yui3-menu-content\">\n");
	printf("<ul class=\"first-of-type\">\n");
	printf("\n");

	printf("<li class=\"yui3-menuitem\"><a class=\"yui3-menuitem-content\" href=\"system.cgi?tjid%ld.%ld\">System</a></li>\n", (long)tv.tv_sec, (long)tv.tv_usec);
	printf("<li class=\"yui3-menuitem\"><a class=\"yui3-menuitem-content\" href=\"adddisk.cgi?tjid%ld.%ld\">Physical Storage</a></li>\n", (long)tv.tv_sec, (long)tv.tv_usec);
	printf("<li class=\"yui3-menuitem\"><a class=\"yui3-menuitem-content\" href=\"liststoragepool.cgi?tjid%ld.%ld\">Storage Pools</a></li>\n", (long)tv.tv_sec, (long)tv.tv_usec);
	printf("<li class=\"yui3-menuitem\"><a class=\"yui3-menuitem-content\" href=\"listvtl.cgi?tjid%ld.%ld\">Virtual Libraries</a></li>\n", (long)tv.tv_sec, (long)tv.tv_usec);
	printf("<li class=\"yui3-menuitem\"><a class=\"yui3-menuitem-content\" href=\"listvdrive.cgi?tjid%ld.%ld\">Virtual Drives</a></li>\n", (long)tv.tv_sec, (long)tv.tv_usec);

	printf("</ul>\n");
	printf("</div>\n");
	printf("</div>\n");
}

void
cgi_refresh(char *cgiscript, char *title, char *msg, int timeout)
{
	char meta[128];

	sprintf(meta, "<meta http-equiv=\"refresh\" content=\"%d; url=%s\" />", timeout, cgiscript);
	__cgi_print_header(title, NULL, 0, meta, 0, NULL);
	cgi_print_div_start("center");
	printf("<div class=\"info\"><p>%s. Please Wait ...</p></div>\n", msg);
	cgi_print_div_end();
	cgi_print_div_trailer();
	cgi_print_body_trailer();
}

void
cgi_print_header(char *title, char *jsfile, int nocache)
{
	__cgi_print_header(title, jsfile, nocache, NULL, 0, NULL);
}

void
cgi_print_div_trailer(void)
{
	printf("</div>\n");
	printf("</div>\n");

	printf("<script type=\"text/javascript\">\n");
	printf("YUI().use('node-menunav', function(Y) {\n");
	printf("var menu = Y.one(\"#quadstormenu\");\n");
	printf("menu.plug(Y.Plugin.NodeMenuNav);\n");
	printf("});\n");
	printf("</script>\n");

}

void
cgi_print_body_trailer(void)
{
	printf("</body>\n");
	printf("</html>\n");
}

void
cgi_print_thdr(char *msg)
{
	printf("<div class=\"thdr\"><span>%s</span></div>\n", msg);
}

void
cgi_print_table_div(char *table)
{
	printf("<div id=\"%s\" style=\"text-align: center;\"></div>\n", table);
}

void
cgi_print_table_start(char *table, char *cols[], int sortable)
{
	int i = 0;
	printf("<script type=\"text/javascript\">\n");
	if (sortable)
		printf("YUI().use('datatable-sort', function (Y) {\n");
	else
		printf("YUI().use('datatable-base', function (Y) {\n");
	printf("var data = [];\n");
	printf("var table = new Y.DataTable({\n");
	printf("columns: [");
	while (cols[i]) {
		if (i)
			printf(", ");
		if (cols[i][0] != '{')
			printf("\"%s\"", cols[i]);
		else
			printf("%s", cols[i]);
		i++;
	}
	printf("],\n");
	printf("data: data\n");
	printf("});\n");
}

void
cgi_print_table_end(char *table)
{
	printf("table.render(\"#%s\");\n", table);
	printf("});\n");
	printf("</script>\n");
}

void
cgi_print_row_start(void)
{
	printf("table.data.add({ ");
}

void
cgi_print_row_end(void)
{
	printf(" });\n");
}

void
cgi_print_comma(void)
{
	printf(", ");
}

void
cgi_print_column(char *name, char *value)
{
	printf("%s: '%s'", name, value);
}

char *
cgi_strip_newline(char *str)
{
	char *tmp;

	while ((tmp = strchr(str, '\n')) != NULL) {
		*tmp = 0;
	}
	return str;
}

void
cgi_print_div_padding(int pad)
{
	printf("<div style=\"padding: %dpx;\">\n", pad);
}

void
cgi_print_div_container(int width, char *align)
{
	printf("<div style=\"float: %s; width: %d%%\">\n", align, width);

}

void
cgi_print_div_start(char *align)
{
	printf("<div style=\"text-align: %s;\">\n", align);
}

void
cgi_print_div_end(void)
{
	printf("</div>\n");
}

void
cgi_print_form_start_check(char *name, char *action, char *method, char *func)
{
	printf("<form id=\"%s\" action=\"%s\" method=\"%s\" onSubmit=\"return %s();\">\n", name, action, method, func);
}

void
cgi_print_form_start(char *name, char *action, char *method, int checkform)
{
	if (!checkform)
		printf("<form id=\"%s\" action=\"%s\" method=\"%s\">\n", name, action, method);
	else
		printf("<form id=\"%s\" action=\"%s\" method=\"%s\" onSubmit=\"return checkform();\">\n", name, action, method);
}

void
cgi_print_form_end(void)
{
	printf("</form>\n");
}

void
cgi_print_break(void)
{
	printf("<br/>\n");
}

void
cgi_print_paragraph(char *str)
{
	printf("<p>%s</p>\n", str);
}

void
cgi_print_submit_button(char *name, char *value)
{
#if 0
	printf("<div><input type=\"submit\" class=\"yui3-button\" name=\"%s\" value=\"%s\" /></div>", name, value);
#endif
//	printf("<div style=\"text-align: center;\"><input type=\"submit\" class=\"inputt\" name=\"%s\" value=\"%s\" /></div>", name, value);
	printf("<div style=\"text-align: center;margin-top: 3px;margin-bottom: 3px;\"><input type=\"submit\" class=\"yui3-button\" name=\"%s\" value=\"%s\" /></div>", name, value);
}

void
cgi_print_checkbox_input(char *name, int checked)
{
	if (checked)
		printf("<input type=\"checkbox\" name=\"%s\" class=\"inputt\" checked=\"checked\">", name);
	else
		printf("<input type=\"checkbox\" name=\"%s\" class=\"inputt\" >", name);
}

void
cgi_print_checkbox_input_td(char *name, int checked)
{
	printf("<td>");
	cgi_print_checkbox_input(name, checked);
	printf("</td>");
}

void
cgi_print_text_input(char *name, int size, char *value, int maxlength)
{
	printf("<input type=\"text\" name=\"%s\" class=\"inputt\" size=\"%d\" value=\"%s\" maxlength=\"%d\">", name, size, value, maxlength);
}

void
cgi_print_text_input_td(char *name, int size, char *value, int maxlength)
{
	printf("<td>");
	cgi_print_text_input(name, size, value, maxlength);
	printf("</td>");
}

void
__cgi_print_error_page(char *msg)
{
	cgi_print_div_start("center");
	printf("<div class=\"error\">ERROR: %s</div>\n", msg);
	cgi_print_div_end();
	cgi_print_div_trailer();
	cgi_print_body_trailer();
	exit(0);
}

void
__cgi_print_header_error_page(char *msg)
{
	cgi_print_header("Configuration Error", NULL, 1);
	cgi_print_div_start("center");
	printf("<div class=\"error\">ERROR: %s</div>\n", msg);
	cgi_print_div_end();
	cgi_print_div_trailer();
	cgi_print_body_trailer();
	exit(0);
}

void
cgi_redirect(char *cgiscript)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	html_header();
	printf("<html>\n");
	printf("<head>\n");
	printf("<meta http-equiv=\"content-type\" content=\"text/html;charset=UTF-8\" />\n");
	if (strchr(cgiscript, '?'))
		printf("<meta http-equiv=\"refresh\" content=\"0; url=%s&tjid=%ld.%ld\" />", cgiscript, (long)tv.tv_sec, (long)tv.tv_usec);
	else
		printf("<meta http-equiv=\"refresh\" content=\"0; url=%s?tjid=%ld.%ld\" />", cgiscript, (long)tv.tv_sec, (long)tv.tv_usec);
	printf("</head>\n");
	printf("</html>\n");
	exit(0);
}

