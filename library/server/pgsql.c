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
#include <time.h>
#include <apicommon.h>
#include <physlib.h>
#include <libpq-fe.h>

#define CONN_STRING		"dbname=qsdb user=vtdbuser password=vtdbuser port=9989"

PGconn *
pgsql_make_conn(void)
{
	PGconn *conn;

	conn = PQconnectdb(CONN_STRING); 
	if (PQstatus(conn) != CONNECTION_OK) {
		DEBUG_ERR_SERVER("Connect to db failed error is %s\n", PQerrorMessage(conn));
		PQfinish(conn);
		return NULL;
	}
	return conn;
}

PGresult *
__pgsql_exec_query(char *conn_string, char *sqlcmd, PGconn **ret_conn)
{
	PGconn *conn;
	PGresult *res;

	conn = PQconnectdb(conn_string); 
	if (PQstatus(conn) != CONNECTION_OK) {
		DEBUG_ERR_SERVER("Connect to db failed error is %s\n", PQerrorMessage(conn));
		PQfinish(conn);
		return NULL;
	}

	res = PQexec(conn, sqlcmd);
	if (PQresultStatus(res) >= PGRES_BAD_RESPONSE) {
		DEBUG_ERR_SERVER("sqlcmd %s failed error is %s status is %d\n", sqlcmd, PQerrorMessage(conn), PQresultStatus(res));
		goto err;
	}

	*ret_conn = conn;
	return res;
err:
	PQclear(res);
	PQfinish(conn);
	return NULL;
}

PGresult *pgsql_exec_query(char *sqlcmd, PGconn **ret_conn)
{
	return __pgsql_exec_query(CONN_STRING, sqlcmd, ret_conn);
}

int
pgsql_rollback(PGconn *conn)
{
	PGresult *res;
	int retval;

	res = PQexec(conn, "ROLLBACK");
	if (PQresultStatus(res) >= PGRES_BAD_RESPONSE)
	{
		DEBUG_ERR_SERVER("Unable to commit the transaction, error is %s status is %d\n", PQerrorMessage(conn), PQresultStatus(res));
		retval = -1;
	}
	else
	{
		retval = 0;
	}

	PQclear(res);
	PQfinish(conn);
	return retval;
}

int
pgsql_commit(PGconn *conn)
{
	PGresult *res;

	res = PQexec(conn, "COMMIT");
	if (PQresultStatus(res) >= PGRES_BAD_RESPONSE)
	{
		DEBUG_ERR_SERVER("Unable to commit a new transaction, error is %s status is %d\n", PQerrorMessage(conn), PQresultStatus(res));
		PQclear(res);
		pgsql_rollback(conn);
		return -1;
	}
	PQclear(res);
	PQfinish(conn);
	return 0;
}

PGconn *
__pgsql_begin(char *conn_string)
{
	PGconn *conn;
	PGresult *res;

	conn = PQconnectdb(conn_string);
	if (PQstatus(conn) != CONNECTION_OK)
	{
		DEBUG_ERR_SERVER("Connect to db failed error is %s\n", PQerrorMessage(conn));
		PQfinish(conn);
		return NULL;
	}
	res = PQexec(conn, "BEGIN");
	if (PQresultStatus(res) >= PGRES_BAD_RESPONSE)
	{
		DEBUG_ERR_SERVER("Unable to start a new transaction, error is %s status is %d\n", PQerrorMessage(conn), PQresultStatus(res));
		PQclear(res);
		PQfinish(conn);
		return NULL;
	}
	PQclear(res);
	return conn;
}

PGconn *
pgsql_begin(void)
{
	return __pgsql_begin(CONN_STRING);
}

uint64_t 
pgsql_exec_query3(PGconn *conn, char *sqlcmd, int isinsert, int *error, char *table, char *seqcol)
{
	uint64_t id = 0;
	PGresult *res;

	*error = 0;
	res = PQexec(conn, sqlcmd);
	if (PQresultStatus(res) >= PGRES_BAD_RESPONSE)
	{
		DEBUG_ERR_SERVER("sqlcmd %s failed: error is %s status is %d\n", sqlcmd, PQerrorMessage(conn), PQresultStatus(res));
		PQclear(res);
		*error = -1;
		return 0;
	}
	PQclear(res);

	if (isinsert)
	{
		char cmd[256];

		snprintf(cmd, sizeof(cmd), "SELECT CURRVAL('%s_%s_seq')", table, seqcol);
		res = PQexec(conn, cmd);
		if (PQresultStatus(res) >= PGRES_BAD_RESPONSE)
		{
			DEBUG_ERR_SERVER("Unable to get insert id error is %s result is %d\n", PQerrorMessage(conn), PQresultStatus(res));
			PQclear(res);
			*error = -1;
			return 0;
		}
		id = strtoull(PQgetvalue(res, 0, 0), NULL, 10);
		PQclear(res);
		if (!id)
		{
			DEBUG_ERR_SERVER("Invalid ID\n");
			*error = -1;
			return 0;
		}
	}
	return id;
}


uint64_t 
__pgsql_exec_query2(char *conn_string, char *sqlcmd, int isinsert, int *error, char *table, char *seqcol)
{
	PGconn *conn;
	uint64_t id = 0;
	PGresult *res;

	*error = 0;
	conn = PQconnectdb(conn_string); 
	if (PQstatus(conn) != CONNECTION_OK)
	{
		DEBUG_ERR_SERVER("Connect to db failed error is %s\n", PQerrorMessage(conn));
		PQfinish(conn);
		*error = -1;
		return 0;
	}

	if (isinsert)
	{
		res = PQexec(conn, "BEGIN");
		if (PQresultStatus(res) >= PGRES_BAD_RESPONSE)
		{
			DEBUG_ERR_SERVER("Unable to start a new transaction, error is %s status is %d\n", PQerrorMessage(conn), PQresultStatus(res));
			PQclear(res);
			PQfinish(conn);
			*error = -1;
			return 0;
		}
		PQclear(res);
	}

	res = PQexec(conn, sqlcmd);
	if (PQresultStatus(res) >= PGRES_BAD_RESPONSE)
	{
		if (isinsert)
		{
			PQclear(res);
			goto inserr;
		}

		DEBUG_ERR_SERVER("sqlcmd %s failed: error is %s status is %d\n", sqlcmd, PQerrorMessage(conn), PQresultStatus(res));
		PQclear(res);
		PQfinish(conn);
		*error = -1;
		return 0;
	}
	PQclear(res);

	if (isinsert)
	{
		char cmd[256];

		snprintf(cmd, sizeof(cmd), "SELECT CURRVAL('%s_%s_seq')", table, seqcol);
		res = PQexec(conn, cmd);
		if (PQresultStatus(res) >= PGRES_BAD_RESPONSE)
		{
			DEBUG_ERR_SERVER("Unable to get insert id error is %s result is %d\n", PQerrorMessage(conn), PQresultStatus(res));
			PQclear(res);
			goto inserr;
		}
		id = strtoull(PQgetvalue(res, 0, 0), NULL, 10);
		if (!id)
		{
			DEBUG_ERR_SERVER("Invalid ID\n");
			PQclear(res);
			goto inserr;
		}
		PQclear(res);

		res = PQexec(conn, "COMMIT");
		if (PQresultStatus(res) >= PGRES_BAD_RESPONSE)
		{
			DEBUG_ERR_SERVER("Unable to commit a new transaction, error is %s status is %d\n", PQerrorMessage(conn), PQresultStatus(res));
			PQclear(res);
			goto inserr;
		}
		PQclear(res);

	}
	PQfinish(conn);
	return id;

inserr:
	res = PQexec(conn, "ROLLBACK");
	PQclear(res);
	PQfinish(conn);
	*error = -1;
	return 0;
}

uint64_t 
pgsql_exec_query2(char *sqlcmd, int isinsert, int *error, char *table, char *seqcol)
{
	return __pgsql_exec_query2(CONN_STRING, sqlcmd, isinsert, error, table, seqcol);
}
