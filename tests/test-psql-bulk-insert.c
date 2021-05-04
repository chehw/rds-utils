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

// test bulk copy: can increase the speed by about 30~200 times or more
/* steps when processing bulk insert:
 * ( https://stackoverflow.com/questions/12206600/how-to-speed-up-insertion-performance-in-postgresql)
 * 1. disable any triggers
 * 2. Drop indexes before starting the import
 * 3. USE COPY instead of INSERT
 * ...
 * 
 * eg. 
 * 	COPY test_schema.users FROM /server-path/test_schema.users.dat
 *  COPY test_schema.users FROM STDIN
 *  COPY test_schema.users FROM STDIN BINARY
 */

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
	char user_id[40];	// size should be greater than (32 hex digits + 4 dashes)
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

#define _TEST_INSERT 
static const int num_records = 100 * 1000;
static user_record_t user[1];
static double time_elapsed = 0.0;
static app_timer_t * timer;

void test_normal_inserting_with_prepared_stmt(psql_context_t * psql);
void test_copy_from_text_format(psql_context_t * psql);
void test_copy_from_binary_format(psql_context_t * psql);
int main(int argc, char **argv)
{
	psql_context_t * psql = init_connection(argc, argv, NULL);
	//printf("ddl: '%s'\n", sql_ddl);
	
	// create schemas and tables from DDL file
	int rc = psql_execute(psql, sql_ddl, NULL);
	assert(0 == rc);
	
	timer = app_timer_get_default();
	
	// test normal inserting
	if(argc > 1 && argv[1][0] == '1') {
		/*
		 * run with command-line args: "1"
		 * $ tests/test-psql-bulk-insert 1
		 * Beware! very slow!
		*/
		test_normal_inserting_with_prepared_stmt(psql);
	}
	
	test_copy_from_text_format(psql);
	
	test_copy_from_binary_format(psql);
	
	psql_context_cleanup(psql);
	free(psql);
	return 0;
}

void test_normal_inserting_with_prepared_stmt(psql_context_t * psql)
{
	debug_printf("==== %s(%p) ====\n", __FUNCTION__, psql);
	int rc = 0;
	
	// truncate table before insert
	rc = psql_execute(psql, "TRUNCATE TABLE " TABLE_NAME ";", NULL);
	assert(0 == rc);
	
	rc = psql_execute(psql, "BEGIN;", NULL);
	assert(0 == rc);
	
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
	
	rc = psql_execute(psql, "COMMIT;", NULL);
	assert(0 == rc);
	
	// clear records
	rc = psql_execute(psql, "TRUNCATE TABLE " TABLE_NAME ";", NULL);
	assert(0 == rc);
#undef NUM_PARAMS
	return;
}

void test_copy_from_text_format(psql_context_t * psql)
{
	debug_printf("==== %s(%p) ====\n", __FUNCTION__, psql);
	static const char * copy_command = "COPY " TABLE_NAME "(user_name, email, password) "
		" FROM STDIN "
		//~ " WITH FORMAT text "	// delim == '\t'
		";";

	int rc = 0;
	psql_result_t res = NULL;
	ExecStatusType status = PGRES_BAD_RESPONSE;
	
	// truncate table before insert
	rc = psql_execute(psql, "TRUNCATE TABLE " TABLE_NAME ";", NULL);
	assert(0 == rc);
	
	rc = psql_execute(psql, "BEGIN;", NULL);
	assert(0 == rc);
	
	rc = psql_execute(psql, copy_command, res);
	assert(0 == rc);
	psql_result_clear(&res);
	
	auto_buffer_t _buf[1];
	auto_buffer_t * buf = auto_buffer_init(_buf, 0);
	app_timer_start(timer);
	
	PGconn * conn = *(PGconn **)psql;
	int batch_size = num_records;
	int records_left = num_records;
	int ok = 0;
	// prepare data, can be processed by other threads
	
	while(records_left > 0) {
		printf("records_left: %d\n", records_left);
		if(records_left < batch_size) batch_size = records_left;
		for(int i = 0; i < batch_size; ++i) {
			memset(user, 0, sizeof(user));
			
			snprintf(user->user_name, sizeof(user->user_name), "user-%.9d", i + num_records);
			snprintf(user->email, sizeof(user->email), "%s@test.com", user->user_name);
			snprintf(user->password, sizeof(user->password), "%.9d", i + num_records);
			
			char line[1024] = "";
			int cb = snprintf(line, sizeof(line), "%s\t%s\t%s\n", user->user_name, user->email, user->password);
			assert(cb > 0);
			
			auto_buffer_push(buf, line, cb);
		}
		ok = PQputCopyData(conn, (char *)buf->data, buf->length);
		assert(ok);
		res = PQgetResult(conn);
		status = PQresultStatus(res);
		assert(status == PGRES_COPY_IN);
		psql_result_clear(&res);
	
		records_left -= batch_size;
		buf->length = 0;
		buf->start_pos = 0;
	}

	auto_buffer_cleanup(buf);

	ok = PQputCopyEnd(conn, NULL);
	assert(ok);
	res = PQgetResult(conn);
	status = PQresultStatus(res);
	//~ assert(status == PGRES_COPY_IN);
	printf("--> status: %s\n", PQresStatus(status));
	psql_result_clear(&res);
	
	time_elapsed = app_timer_stop(timer);
	printf("time_elapsed: %.6f ms\n", time_elapsed * 1000.0);
	
	rc = psql_execute(psql, "COMMIT;", NULL);
	assert(0 == rc);
	return;
}


#include <stdint.h>
#include <endian.h>

struct psql_binary_header
{
	uint8_t signature[11];
	uint32_t flags;	// big-endian, always zero
	uint32_t extensions[1];
}__attribute__((packed));

struct psql_field_binary
{
	uint32_t length; // big-endian
	unsigned char data[0];
}__attribute__((packed));

struct psql_tuple_binary
{
	uint16_t num_fields; // big-endian
	struct psql_field_binary fields[0];	
}__attribute__((packed));


void test_copy_from_binary_format(psql_context_t * psql)
{
	debug_printf("==== %s(%p) ====\n", __FUNCTION__, psql);
	static const char * copy_command = "COPY " TABLE_NAME "(user_name, email, password) "
		//~ " FROM '/1.dump' "
		" FROM STDIN "
		" BINARY "
		";";
	
	int rc = 0;
	// truncate table before insert
	rc = psql_execute(psql, "TRUNCATE TABLE " TABLE_NAME ";", NULL);
	assert(0 == rc);
	
	rc = psql_execute(psql, "BEGIN;", NULL);
	assert(0 == rc);
	
	psql_result_t res = NULL;
	rc = psql_execute(psql, copy_command, res);
	assert(0 == rc);
	psql_result_clear(&res);
	
	auto_buffer_t _buf[1];
	auto_buffer_t * buf = auto_buffer_init(_buf, 0);
	PGconn * conn = *(PGconn **)psql;
	ExecStatusType status = PGRES_BAD_RESPONSE;
	int ok = FALSE;
	static struct psql_binary_header hdr= {
		.signature = "PGCOPY\n\377\r\n",
	};
	assert(19 == sizeof(hdr));
	
	for(int i = 0; i < sizeof(hdr); ++i) {
		printf("%.2x ", hdr.signature[i]);	// since it does not exceed the size of hdr, this kind of out-of-bounds is safe
	}
	printf("\n");
	
	// dump data to a tmp file
	FILE * fp = fopen("/tmp/1.dump", "w+");
	fwrite(&hdr, 1, sizeof(hdr), fp);
	
	// write psql_binary_header
	ok = PQputCopyData(conn, (char *)&hdr, sizeof(hdr));
	assert(ok);
	res = PQgetResult(conn);
	status = PQresultStatus(res);
	printf("status: %s\n", PQresStatus(status));
	psql_result_clear(&res);
	
	
	app_timer_start(timer);
	int batch_size = num_records;		
	int records_left = num_records;

	// write binary tuples
	while(records_left > 0) {
		if(records_left < batch_size) batch_size = records_left;
		
		uint32_t length  = 0;
		uint16_t num_fields = htobe16(3);
		for(int i = 0; i < batch_size; ++i) {
			memset(user, 0, sizeof(user));
			uint32_t cb_user = snprintf(user->user_name, sizeof(user->user_name), "user-%.9d", i + num_records);
			uint32_t cb_email = snprintf(user->email, sizeof(user->email), "%s@test.com", user->user_name);
			uint32_t cb_password = snprintf(user->password, sizeof(user->password), "%.9d", i + num_records);
		
			auto_buffer_push(buf, &num_fields, 2);	// push num fields
			
			length = htobe32(cb_user);
			auto_buffer_push(buf, &length, sizeof(length));
			auto_buffer_push(buf, user->user_name, cb_user);
			
			length = htobe32(cb_email);
			auto_buffer_push(buf, &length, sizeof(length));
			auto_buffer_push(buf, user->email, cb_email);
			
			length = htobe32(cb_password);
			auto_buffer_push(buf, &length, sizeof(length));
			auto_buffer_push(buf, user->password, cb_password);
		}
		
		fwrite(buf->data, 1, buf->length, fp);
		
		
		ok = PQputCopyData(conn, (char *)buf->data, buf->length);
		assert(ok);
		
		res = PQgetResult(conn); 
		status = PQresultStatus(res);
		printf("bytes copied: %ld\n", (long)buf->length);
		printf("put-data: status: %s\n", PQresStatus(status));
		psql_result_clear(&res);
	
		// reset buffer
		buf->length = 0;
		buf->start_pos = 0;
		records_left -= batch_size;
	}
	// write file trailer
	static const uint16_t file_trailer = 0xFFFF;
	ok = PQputCopyData(conn, (char *)&file_trailer, 2);
	
	fwrite(&file_trailer, 1, 2, fp);
	fclose(fp);
	
	assert(ok);
	res = PQgetResult(conn); 
	status = PQresultStatus(res);
	printf("put-trailer: status: %s\n", PQresStatus(status));
	psql_result_clear(&res);
		
	
	ok = PQputCopyEnd(conn, NULL);
	res = PQgetResult(conn); 
	status = PQresultStatus(res);
	printf("status: %s\n", PQresStatus(status));
	psql_result_clear(&res);
		
		
	time_elapsed = app_timer_stop(timer);
	printf("time_elapsed: %.6f ms\n", time_elapsed * 1000.0);
	
	
	rc = psql_execute(psql, "COMMIT;", NULL);
	assert(0 == rc);
	
	//~ res = PQgetResult(conn);
	//~ ExecStatusType status = PQresultStatus(res);
	//~ printf("status: %s\n", PQresStatus(status));
	//~ psql_result_clear(&res);
	
	auto_buffer_cleanup(buf);
	return;
}
