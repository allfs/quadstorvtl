#ifndef QUADSTOR_PGSQL_H_
#define QUADSTOR_PGSQL_H_
#include <physlib.h>
#include <libpq-fe.h>

extern PGresult *pgsql_exec_query(char *sqlcmd, PGconn **ret_conn);
uint64_t pgsql_exec_query2(char *sqlcmd, int isinsert, int *error, char *table, char *seqcol);
PGconn *pgsql_make_conn(void);
extern uint64_t pgsql_exec_query3(PGconn *conn, char *sqlcmd, int isinsert, int *error, char *table, char *seqcol);
extern PGconn *pgsql_begin(void);
extern int pgsql_commit(PGconn *conn);
extern int pgsql_rollback(PGconn *conn);

#endif
