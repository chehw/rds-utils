#ifndef RDB_POSTGRES_H_
#define RDB_POSTGRES_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include "avl_tree.h"

#define PSQL_PREPARE_PARAMS_MAX_LEN (256)
typedef struct psql_prepare_params
{
	char stmt_name[PSQL_PREPARE_PARAMS_MAX_LEN];
	int num_params;
	unsigned int * types;
}psql_prepare_params_t;

typedef struct psql_params
{
	int num_params;
	unsigned int * types;
	const char ** values;
	int * cb_values;
	int * value_formats;	// 0: text; 1: binary
	int result_format;		// 0: text; 1: binary
}psql_params_t;
psql_params_t * psql_params_init(psql_params_t * params, int num_params, int result_format);
void psql_params_cleanup(psql_params_t * params);
int psql_params_setv(psql_params_t * params, 
	int num_params, int result_format,
	unsigned int type, const char * value, const int cb_value, const int value_format,
	...);



typedef void * psql_result_t;
void psql_result_clear(psql_result_t * p_result);
const char * psql_result_get_value(const psql_result_t res, int row, int col);
int psql_result_get_count(const psql_result_t res);
int psql_result_get_fields(const psql_result_t res, const char *** p_fields);
const char * psql_result_strerror(const psql_result_t res);

typedef struct psql_context psql_context_t;
psql_context_t * psql_context_init(psql_context_t * psql, void * user_data);
void psql_context_cleanup(psql_context_t * psql);

int psql_connect_db(psql_context_t * psql, const char * sz_conn, int async_mode);
int psql_connect_async_wait(psql_context_t * psql, int64_t timeout_ms);
int psql_disconnect(psql_context_t * psql);

int psql_execute(psql_context_t * psql, const char * command, void ** p_result);
int psql_exec_params(psql_context_t * psql, const char * command, psql_params_t * params, psql_result_t * p_result);
int psql_prepare(psql_context_t * psql, const char * query, const char * stmt_name, const int num_params, const unsigned int * types);
int psql_exec_prepared(psql_context_t * psql, const char * stmt_name, const psql_params_t * params, psql_result_t * p_result);

#ifdef __cplusplus
}
#endif
#endif

