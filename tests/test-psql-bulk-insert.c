/*
 * test-psql-cursor.c
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

#include <limits.h>
#include "rdb-postgres.h"
#include <stdarg.h>
#include <libpq-fe.h>

#include "app_timer.h"
#include "utils.h"


#define SCHEMA_NAME 	"test_schema"
#define TABLE_NAME 		SCHEMA_NAME".users"

static const char * sql_ddl = 
//~ "CREATE EXTENSION IF NOT EXISTS \"uuid-ossp\";"
"BEGIN; "
"DROP TABLE IF EXISTS " TABLE_NAME ";"
"DROP SCHEMA IF EXISTS " SCHEMA_NAME ";"
"CREATE SCHEMA " SCHEMA_NAME ";"
"CREATE TABLE " TABLE_NAME "("
//~ "	user_id		UUID			NOT NULL DEFAULT uuid_generate_v1 () PRIMARY KEY, "
"	user_id		UUID			NOT NULL DEFAULT gen_random_uuid () PRIMARY KEY, "
"	user_name	VARCHAR(100)	NOT NULL UNIQUE, "
"	email		VARCHAR(100)	NOT NULL, "
"	password	VARCHAR(100)	NOT NULL, "
"	ctime		TIMESTAMP		NOT NULL DEFAULT CURRENT_TIMESTAMP, "
"	mtime		TIMESTAMP "
");"
"END;";


struct user_record
{
	char user_id[40];	// size should greater than (32 hex digits + 4 dashes)
	char user_name[128];
	char email[256];
	char password[128];
	char ctime[32];	// create time
	char mtime[32];	// modify time
}__attribute__((packed));
typedef struct user_record user_record_t;

static psql_context_t * init_connection(int argc, char ** argv, void * user_data)
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
	
	psql_context_t * psql = psql_context_init(NULL, user_data);
	int rc = psql_connect_db(psql, sz_conn, 0);
	assert(0 == rc);
	
	return psql;
}

#include <libpq-fe.h>
#include "auto_buffer.h"

int main(int argc, char **argv)
{
	psql_context_t * psql = init_connection(argc, argv, NULL);
	//printf("ddl: '%s'\n", sql_ddl);
	
	// create schemas and tables from DDL file
	int rc = psql_execute(psql, sql_ddl, NULL);
	assert(0 == rc);

//~ int num_records = 100 * 1000;
	int num_records = 100;
	user_record_t user[1];
	
	double time_elapsed = 0.0;
	app_timer_t * timer = app_timer_get_default();
	
#if defined(_TEST_INSERT)
	
	#define NUM_PARAMS (3)
	static const char * insert_command = "insert into " 
		TABLE_NAME 
		"(user_name, email, password) "
		"values($1,$2,$3);";
	
	static const char * insert_stmt_name = "insert-users";
	psql_prepare_params_t prepare_params[1] = {{
		.stmt_name = insert_stmt_name,
		.num_params = 3, 
		.types = NULL,	// let server-end to deduce the type 
	}};
	psql_params_t query_params[1] = {{ .num_params = NUM_PARAMS }};
	const char * param_values[NUM_PARAMS] = {NULL};
	
	query_params->values = param_values;	// attach pointers to psql_params_t (eliminates the need to alloc/free memory)
	query_params->cb_values = NULL;			// no need for text data
	query_params->value_formats = NULL;		// default 
	
	rc = psql_prepare(psql, insert_command, prepare_params);
	assert(0 == rc);
	
	app_timer_start(timer);
	// insert records
	for(int i = 0; i < num_records; ++i) {
		memset(user, 0, sizeof(user));
		
		snprintf(user->user_name, sizeof(user->user_name), "user-%.9d", i);
		snprintf(user->email, sizeof(user->email), "%s@test.com", user->user_name);
		snprintf(user->password, sizeof(user->password), "%.9d", i);
		
		// set values
		param_values[0] = user->user_name;
		param_values[1] = user->email;
		param_values[2] = user->password;
		
		psql_result_t res = NULL;
		rc = psql_exec_prepared(psql, insert_stmt_name, query_params, &res);
		assert(0 == rc);
		psql_result_clear(&res);
	}
	time_elapsed = app_timer_stop(timer);
	printf("time_elapsed: %.6f ms\n", time_elapsed * 1000.0);
	// test inserting with prepared stmt
#endif
	
	
	// test bulk copy: can increase the speed by about 30~200 times or more
	/* steps when processing bulk insert:
	 * ( https://stackoverflow.com/questions/12206600/how-to-speed-up-insertion-performance-in-postgresql)
	 * 1. disable any triggers
	 * 2. Drop indexes before starting the import
	 * 3. USE COPY instead of INSERT
	 * ...
	 * 
	 * eg. 
	 * 	COPY test_schema.users FROM /home/username/testdata/test_schema.users.dat
	 *  COPY test_schema.users FROM /home/username/testdata/test_schema.users.dat
	 */
	
	static const char * copy_command = "COPY " TABLE_NAME "(user_name, email, password) "
		" FROM STDIN "
		//~ " WITH FORMAT text "	// delim == '\t'
		";";
	
	psql_result_t res = NULL;
	rc = psql_execute(psql, copy_command, res);
	assert(0 == rc);
	psql_result_clear(&res);
	
	auto_buffer_t _buf[1];
	auto_buffer_t * buf = auto_buffer_init(_buf, 0);
	
	// prepare data, can be processed by other threads
	for(int i = 0; i < num_records; ++i) {
		memset(user, 0, sizeof(user));
		
		snprintf(user->user_name, sizeof(user->user_name), "user-%.9d", i + num_records);
		snprintf(user->email, sizeof(user->email), "%s@test.com", user->user_name);
		snprintf(user->password, sizeof(user->password), "%.9d", i + num_records);
		
		char line[1024] = "";
		int cb = snprintf(line, sizeof(line), "%s\t%s\t%s\n", user->user_name, user->email, user->password);
		assert(cb > 0);
		
		auto_buffer_push(buf, line, cb);
	}
	
	app_timer_start(timer);
	PGconn * conn = *(PGconn **)psql;
	rc = PQputCopyData(conn, (char *)buf->data, buf->length);
	auto_buffer_cleanup(buf);
	assert(1 == rc);
	rc = PQputCopyEnd(conn, NULL);
	assert(1 == rc);
	
	res = PQgetResult(conn);
	ExecStatusType status = PQresultStatus(res);
	printf("status: %s\n", PQresStatus(status));
	psql_result_clear(&res);
	
	time_elapsed = app_timer_stop(timer);
	printf("time_elapsed: %.6f ms\n", time_elapsed * 1000.0);
	
	psql_context_cleanup(psql);
	free(psql);
	return 0;
}

