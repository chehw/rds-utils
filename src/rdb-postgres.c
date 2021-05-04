/*
 * rdb-postgres.c
 * 
 * Copyright 2021 chehw <hongwei.che@gmail.com>
 * 
 * The MIT License
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of 
 * this software and associated documentation files (the "Software"), to deal in 
 * the Software without restriction, including without limitation the rights to 
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies 
 * of the Software, and to permit persons to whom the Software is furnished to 
 * do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all 
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
 * SOFTWARE.
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#include <stdarg.h>

#include <libpq-fe.h>
#include "avl_tree.h"

#include "rdb-postgres.h"

#define CHLIB_PSQL_VERBOSE (1)
int psql_prepare_params_compare(const void * a, const void * b)
{
	return strcmp((const char *)a, (const char *)b);
}

int psql_params_setv(psql_params_t * params, int num_params, int result_format,
	unsigned int type, const char * value, const int cb_value, const int value_format,
	...)
{
	int index = 0;
	assert(params);
	if(num_params == -1) num_params = params->num_params;
	assert(num_params >= 0);
	psql_params_init(params, num_params, result_format);
	if(num_params == 0) return 0;
	
	params->types[0] = type;
	params->values[0] = value;
	params->cb_values[0] = cb_value;
	params->value_formats[0] = value_format;
	++index;
	
	if(index < num_params) {
		va_list ap;
		va_start(ap, value_format);
		
		while(index < num_params) {
			params->types[index] = va_arg(ap, Oid);
			params->values[index] = va_arg(ap, const char *);
			params->cb_values[index] = va_arg(ap, const int);
			params->value_formats[index] = va_arg(ap, const int);
			++index;
		}
		assert(index == num_params);
		va_end(ap);
	}
	return 0;
}

psql_params_t * psql_params_init(psql_params_t * params, int num_params, int result_format)
{
	if(NULL == params) {
		params = calloc(1, sizeof(*params));
		assert(params);
	}else {
		if(num_params == -1) num_params = params->num_params;
		if(num_params == 0) {
			psql_params_cleanup(params);
			return params;
		}
	}
	if(result_format != -1) params->result_format = result_format;
	
	if(num_params != params->num_params)
	{
		params->num_params = num_params;
		params->types = realloc(params->types, sizeof(*params->types) * num_params);
		params->values = realloc(params->values, sizeof(*params->values) * num_params);
		params->cb_values = realloc(params->cb_values, sizeof(*params->cb_values) * num_params);
		params->value_formats = realloc(params->value_formats, sizeof(*params->value_formats) * num_params);
		assert(params->types && params->values && params->cb_values && params->value_formats);
	}
	
	memset(params->types, 0, sizeof(*params->types) * num_params);
	memset(params->values, 0, sizeof(*params->values) * num_params);
	memset(params->cb_values, 0, sizeof(*params->cb_values) * num_params);
	memset(params->value_formats, 0, sizeof(*params->value_formats) * num_params);
	return params;
}

void psql_params_cleanup(psql_params_t * params)
{
	if(NULL == params) return;
	if(params->types) { free(params->types); params->types = NULL; }
	if(params->values) { free(params->values); params->values = NULL; }
	if(params->cb_values) { free(params->cb_values); params->cb_values = NULL; }
	if(params->value_formats) { free(params->value_formats); params->value_formats = NULL; }
	params->num_params = 0;
	params->result_format = 0;
	return;
}

/*********************************************
 * 
*********************************************/
typedef struct psql_context
{
	PGconn * conn;
	psql_params_t params[1];
	
	ConnStatusType conn_status;
	avl_tree_t named_params_tree[1];
	
	ExecStatusType exec_status;
	char err_msg[PATH_MAX];
}psql_context_t;

psql_context_t * psql_context_init(psql_context_t * psql, void * user_data)
{
	if(NULL == psql) psql = calloc(1, sizeof(*psql));
	assert(psql);
	
	avl_tree_t * tree = avl_tree_init(psql->named_params_tree, psql);
	assert(tree && tree == psql->named_params_tree);
	tree->on_free_data = free;
	
	return psql;
}
void psql_context_cleanup(psql_context_t * psql) 
{
	if(NULL == psql) return;
	
	PGconn * conn = psql->conn;
	if(conn) {
		psql->conn = NULL;
		PQfinish(conn);
	}
	avl_tree_cleanup(psql->named_params_tree);
	return;
}

int psql_disconnect(psql_context_t * psql)
{
	if(NULL == psql || NULL == psql->conn) return 0;
	
	PQfinish(psql->conn);
	psql->conn = NULL;
	return 0;
}

int psql_connect_db(psql_context_t * psql, const char * sz_conn, int async_mode)
{
	PGconn * conn = NULL;
	if(NULL == sz_conn) sz_conn = "dbname=postgres";

	if(async_mode) {
		conn = PQconnectStart(sz_conn);
		psql->conn_status = PQstatus(conn);
	#if defined(CHLIB_PSQL_VERBOSE) // dump current status
		char msg[200] = "";
		
		switch(psql->conn_status) {
		case CONNECTION_STARTED:
			snprintf(msg, sizeof(msg), "Connection started.");
			break;
		case CONNECTION_MADE:
			snprintf(msg, sizeof(msg), "Connected.");
			break;
		default:
			snprintf(msg, sizeof(msg), "Connecting ... (status=%d).", (int)psql->conn_status);
			break;
		}
		fprintf(stderr, "%s\n", msg);
	#endif
	}else {
		conn = PQconnectdb(sz_conn);
		if(PQstatus(conn) != CONNECTION_OK) {
			fprintf(stderr, "[ERROR]: connection to db failed: \n"
				"conn_string: '%s'\n"
				"  - err_msg: %s\n",
				sz_conn,
				PQerrorMessage(conn)
			);
			PQfinish(conn);
			return -1;
		}
		
		/* Set always-secure search path, so malicious users can't take control. */
		psql_result_t res = PQexec(conn, 
			//~ "SELECT pg_catalog.set_config('search_path', '', false)");
			"SELECT pg_catalog.set_config('search_path', '\"$user\", public', false)");
			
		if(PQresultStatus(res) != PGRES_TUPLES_OK) {
			fprintf(stderr, "[ERROR]: set_config(search_path) failed: \n"
				"  - err_msg: %s\n",
				PQerrorMessage(conn)
			);
			PQclear(res);
			PQfinish(conn);
			return -1;
		}
		psql_result_clear(&res); 
	}
	if(NULL == conn) return -1;
	psql->conn = conn;
	return 0;
}

int psql_connect_async_wait(psql_context_t * psql, int64_t timeout_ms)
{
	assert(psql && psql->conn);
	assert(timeout_ms > 0);
	
	ConnStatusType conn_status = CONNECTION_AWAITING_RESPONSE;
	PGconn * conn = psql->conn;
	struct timespec timer, remain;
	memset(&timer, 0, sizeof(timer));
	
//	timer->tv_sec = timeout_ms / 1000;
	timer.tv_sec = 0;
	timer.tv_nsec = 100 * 1000000; // check_intervals: 100 ms
	int rc = -1;
	while(timeout_ms > 0) {
		memset(&remain, 0, sizeof(remain));
		rc = nanosleep(&timer, &remain);
		if(rc < 0) {
			if(errno == EINTR) { // The pause has been interrupted by a signal that was delivered to the thread
				timer = remain;
				continue;
			}
			perror("psql_connect_async_wait()::nanosleep");
			break;
		}
		
		// reset timer
		timer.tv_sec = 0;
		timer.tv_nsec = 100 * 1000000; // check_intervals: 100 ms
		PostgresPollingStatusType polling_status = PQconnectPoll(conn);
		if(PGRES_POLLING_FAILED == polling_status) {
			rc = -1;
			conn_status = CONNECTION_BAD;
			break;
		}
		if(polling_status == PGRES_POLLING_OK) {
			conn_status = CONNECTION_OK;
			break;
		}
	}
	psql->conn_status = conn_status;
	if(rc < 0 || conn_status != CONNECTION_MADE) return -1;
	return 0;
}


	
static inline int psql_check_result(psql_context_t * psql, const psql_result_t res)
{
	ExecStatusType status = PQresultStatus(res);
	psql->exec_status = status;
	
	snprintf(psql->err_msg, sizeof(psql->err_msg), "STATUS: %d(%s): %s", 
		status, PQresStatus(status), 
		PQresultErrorMessage(res));
	//fprintf(stderr, "%s\n", psql->err_msg);

	switch(status) {
	case PGRES_COMMAND_OK:
	case PGRES_TUPLES_OK:
	case PGRES_SINGLE_TUPLE:
		return 0;	// ok
	
	case PGRES_EMPTY_QUERY:
	case PGRES_COPY_OUT:
	case PGRES_COPY_IN:
	case PGRES_COPY_BOTH:
	case PGRES_NONFATAL_ERROR:
		return 1;	// ok with informations

	case PGRES_BAD_RESPONSE:
	case PGRES_FATAL_ERROR:
	default:
		break;
	}
	return -1;	// failed
}

#define psql_check_result_on_error_return(conn, res) do { 	\
		if(psql_check_result(psql, res) < 0) {						\
			fprintf(stderr, "[ERROR]: %s\n", psql->err_msg);		\
			return -1;												\
		}															\
	}while(0)
int psql_execute(psql_context_t * psql, const char * command, void ** p_result)
{
	assert(psql && psql->conn);
	PGconn * conn = psql->conn;
	PGresult * result = PQexec(conn, command);
	
	psql_check_result_on_error_return(conn, result);
	
	if(p_result) {
		*p_result = result;
	}else {
		PQclear(result);
	}
	return 0;
}

int psql_exec_params(psql_context_t * psql, const char * command, const psql_params_t * params, void ** p_result)
{
	assert(psql && psql->conn);
	PGconn * conn = psql->conn;
	PGresult * result = PQexecParams(conn, command, params->num_params, 
		params->types, 
		params->values,
		params->cb_values,
		params->value_formats,
		params->result_format
	);
	
	psql_check_result_on_error_return(conn, result);
	
	if(p_result) {
		*p_result = result;
	}else {
		PQclear(result);
	}
	return 0;
}

int psql_prepare(psql_context_t * psql, const char * query, const psql_prepare_params_t * prepare_params)
{
	assert(psql && psql->conn);
	PGconn * conn = psql->conn;
	PGresult * result = PQprepare(conn, prepare_params->stmt_name, query, prepare_params->num_params, prepare_params->types);
	
	if(NULL == result) return -1;
	PQclear(result);
	return 0;
}

int psql_exec_prepared(psql_context_t * psql, const char * stmt_name, const psql_params_t * params, void ** p_result)
{
	assert(psql && psql->conn);
	PGconn * conn = psql->conn;
	assert(params);
	
	PGresult * result = PQexecPrepared(conn, stmt_name, params->num_params, 
		params->values,
		params->cb_values,
		params->value_formats,
		params->result_format
	);
	
	psql_check_result_on_error_return(conn, result);
	if(p_result) {
		*p_result = result;
	}else {
		PQclear(result);
	}
	return 0;
}

void psql_result_clear(psql_result_t * p_result)
{
	if(NULL == p_result || NULL == *p_result) return;
	PQclear(*p_result);
	*p_result = NULL;
	return;
}
const char * psql_result_get_value(const psql_result_t res, int row, int col)
{
	return PQgetvalue(res, row, col);
}
int psql_result_get_count(const psql_result_t res)
{
	return PQntuples(res);
}
int psql_result_get_fields(const psql_result_t res, const char *** p_fields)
{
	int num_fields = PQnfields(res);
	if(num_fields <= 0) return num_fields;
	if(NULL == p_fields) return num_fields;	// return num fields only
	
	const char ** fields = calloc(num_fields, sizeof(*fields));
	assert(fields);
	
	for(int i = 0; i < num_fields; ++i)
	{
		fields[i] = PQfname(res, i);
	}
	*p_fields = fields;
	return num_fields;
}


/* *********************************** **
 * Asynchronous Command Processing
** *********************************** */
int psql_send_query(psql_context_t * psql, const char * command)
{
	assert(psql && psql->conn);
	PGconn * conn = psql->conn;
	psql->err_msg[0] = '\0';
	
	int ok = PQsendQuery(conn, command);
	if(!ok) {
		const char * err_msg = PQerrorMessage(conn);
		if(err_msg) {
			strncpy(psql->err_msg, err_msg, sizeof(psql->err_msg));
		}
		return -1;
	}
	return 0;
}

int psql_send_query_params(psql_context_t * psql, const char * command, const psql_params_t * params)
{
	assert(psql && psql->conn);
	PGconn * conn = psql->conn;
	psql->err_msg[0] = '\0';
	
	int ok = PQsendQueryParams(conn, command, params->num_params, 
		params->types, params->values, params->cb_values, params->value_formats, 
		params->result_format);
	if(!ok) {
		const char * err_msg = PQerrorMessage(conn);
		if(err_msg) {
			strncpy(psql->err_msg, err_msg, sizeof(psql->err_msg));
		}
		return -1;
	}
	return 0;
}

int psql_send_prepare(psql_context_t * psql, const char * query, const char * stmt_name, int num_params, const unsigned int * param_types)
{
	assert(psql && psql->conn);
	PGconn * conn = psql->conn;
	psql->err_msg[0] = '\0';
	
	int ok = PQsendPrepare(conn, stmt_name, query, num_params, param_types);
	if(!ok) {
		const char * err_msg = PQerrorMessage(conn);
		if(err_msg) {
			strncpy(psql->err_msg, err_msg, sizeof(psql->err_msg));
		}
		return -1;
	}
	return 0;
}

int psql_send_query_prepared(psql_context_t * psql, const char * stmt_name, const psql_params_t * params)
{
	assert(psql && psql->conn);
	PGconn * conn = psql->conn;
	psql->err_msg[0] = '\0';
	
	int ok = PQsendQueryPrepared(conn, stmt_name, 
		params->num_params, 
		params->values, params->cb_values, params->value_formats,
		params->result_format);
	if(!ok) {
		const char * err_msg = PQerrorMessage(conn);
		if(err_msg) {
			strncpy(psql->err_msg, err_msg, sizeof(psql->err_msg));
		}
		return -1;
	}
	return 0;
}

enum {
	FAILED = -1,
	NO_MORE_RESULTS = 0,
	MORE_RESULTS = 1
};
int psql_get_result(psql_context_t * psql, psql_result_t * p_result)
{
	assert(psql && psql->conn);
	PGconn * conn = psql->conn;
	psql->err_msg[0] = '\0';
	
	PGresult * res = PQgetResult(conn);
	if(NULL == res) return NO_MORE_RESULTS;	// ok and and there will be no more results.
	
	if(NULL == p_result) {
		PQclear(res);
		return MORE_RESULTS;
	}
	*p_result = res;
	return MORE_RESULTS; // ok and maybe more results available.
}

#if defined(_TEST_RDB_POSTGRES) && defined(_STAND_ALONE)
/* *******************************************************
 * - build:
 *   $ cd {project_dir}
 *   $ tests/make.sh rdb-postgres
 * - run tests:
 *   $ valgrind --leak-check=full tests/rdb-postgres
** ******************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "app_timer.h"


void dump_result(PGresult * res)
{
	int num_fields = PQnfields(res);
	int num_rows = PQntuples(res);
	
	// print columns
	printf("row\t");
	for(int i = 0; i < num_fields; ++i) {
		printf("%s\t", PQfname(res, i));
	}
	printf("\n");
	
	for(int row = 0; row < num_rows; ++row) {
		printf("%.3d\t", row);
		for(int col = 0; col < num_fields; ++col) {
			char *value = PQgetvalue(res, row, col);
			printf("%s\t", value);
		}
		printf("\n");
	}
	return;
}
static psql_context_t g_psql[1];

int test_psql_execute(psql_context_t * psql);
int test_psql_prepare(psql_context_t * psql);
int test_async_query(psql_context_t * psql);

int main(int argc, char **argv)
{
	// load login info from environment variables: 
	const char * user = 	getenv("BAMS_PSQL_DB_USER");
	const char * password = getenv("BAMS_PSQL_DB_PASSWORD");
	const char * host = 	getenv("BAMS_TEST_DB_SERVER");
	const char * port = 	getenv("BAMS_TEST_DB_SERVER_PORT");
	const char * dbname = 	getenv("BAMS_TEST_DBNAME");

	assert(host && user && password);
	if(NULL == port) port = "5432";
	if(NULL == dbname) dbname = "test_db1";
	
	int lib_version = PQlibVersion();
	assert(lib_version > 0);
	
	int major = lib_version / 10000;
	int minor = lib_version % 10000;
	printf("libpq version: %d.%d\n", major, minor);
	
	
	char sz_conn[PATH_MAX] = "";
	int cb = snprintf(sz_conn, sizeof(sz_conn), 
		" host=%s port=%s "
		" dbname=%s user=%s password=%s ",
		host, port,
		dbname, user, password);
	assert(cb > 0);
	
	psql_context_t * psql = psql_context_init(g_psql, NULL);
	int rc = psql_connect_db(psql, sz_conn, 0);
	assert(0 == rc && psql->conn);
	
	test_psql_execute(psql);
	test_psql_prepare(psql);
	
	test_async_query(psql);
	
	
	PQfinish(psql->conn);
	
	
	return 0;
}

int test_psql_execute(psql_context_t * psql)
{
	printf("==== %s(%p) ====\n", __FUNCTION__, psql);
	// list tables under schema::'bams_user'
	static const char * list_tables_command = "select * from pg_tables where schemaname='bams_user';";
	
	psql_result_t res = NULL;
	int rc = 0;
	
	double time_elapsed = 0.0;
	app_timer_t * timer = app_timer_start(NULL);
	
	rc = psql_execute(psql, list_tables_command, &res);
	assert(0 == rc && res);
	assert(res);
	
	time_elapsed = app_timer_stop(timer);
	printf(" --> time elapsed: %.6f ms\n", time_elapsed * 1000.0);
	
	dump_result(res);
	psql_result_clear(&res);
	return 0;
}

int test_psql_prepare(psql_context_t * psql)
{
	printf("==== %s(%p) ====\n", __FUNCTION__, psql);
	// test exec_prepared
	static const char * list_tables_prepare_query = "select * from pg_tables where schemaname=$1; ";
	psql_params_t params_buf[1];
	memset(params_buf, 0, sizeof(params_buf));
	
	psql_result_t res = NULL;
	int rc = 0;
	int num_params = 1;
	psql_params_t * params = psql_params_init(params_buf, num_params, 0);
	assert(params);
	
	char * schema = "bams_user";
	int cb_schema = strlen(schema);
	Oid oid_types[] = {
		[0] = 1043, // oid_varchar == select oid from pg_pgtype where typname = 'varchar'
	};
	
	enum {
		text_format = 0,
		binary_format = 1
	};
	psql_params_setv(params, num_params, text_format, oid_types[0], schema, cb_schema, binary_format);

	char * stmt_name = "list-table";
	psql_prepare_params_t prepare_params[1] = {{
		.stmt_name = stmt_name,
		.num_params = 1,
		.types = oid_types,
	}};
	
	double time_elapsed = 0.0;
	app_timer_t * timer = app_timer_start(NULL);
	
	rc = psql_prepare(psql, list_tables_prepare_query, prepare_params);
	assert(0 == rc);
	
	rc = psql_exec_prepared(psql, stmt_name, params, &res);
	assert(0 == rc);
	
	time_elapsed = app_timer_stop(timer);
	printf(" --> time elapsed: %.6f ms\n", time_elapsed * 1000.0);
	
	dump_result(res);
	
	psql_result_clear(&res);
	psql_params_cleanup(params_buf);
	return 0;
}

int test_async_query(psql_context_t * psql)
{
	printf("==== %s(%p) ====\n", __FUNCTION__, psql);
	// list tables under schema::'bams_user'
	static const char * list_tables_command = "select * from pg_tables where schemaname='bams_user';";
	
	psql_result_t res = NULL;
	int rc = 0;
	
	double time_elapsed = 0.0;
	app_timer_t * timer = app_timer_start(NULL);
	
	rc = psql_send_query(psql, list_tables_command);
	assert(0 == rc);
	
	struct timespec check_intervals = {
		.tv_sec = 0,
		.tv_nsec = 10 * 1000000, // check status every 10 ms
	};
	
	int num_results = 0;
	while(1) {
		rc = psql_get_result(psql, &res);
		if(rc <= 0) break;
		
		time_elapsed = app_timer_stop(timer);
		++num_results;
		printf(" results[%d]--> time elapsed: %.6f ms\n", num_results, time_elapsed * 1000.0);
		
	
		dump_result(res);
		psql_result_clear(&res);
		struct timespec remaining = {
			.tv_sec = 0,
			.tv_nsec = 0,
		};
		nanosleep(&check_intervals, &remaining);
	}
	
	
	
	psql_result_clear(&res);
	return 0;
}
#endif

