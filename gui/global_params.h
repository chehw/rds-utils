#ifndef PSQL_VIEWER_GLOBAL_PARAMS_H_
#define PSQL_VIEWER_GLOBAL_PARAMS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <limits.h>
#include <json-c/json.h>
#include <libintl.h>

#include "shell.h"
#include "rdb-postgres.h"

typedef struct global_params
{
	void * user_data;
	char app_path[PATH_MAX];
	char work_dir[PATH_MAX];
	
	json_object * jconfig;
	char host[256];
	char port[100];
	char dbname[100];
	char user[100];
	char password[100];
	
	psql_context_t * psql;
	shell_context_t * shell;
}global_params_t;
global_params_t * global_params_init(global_params_t * params, int argc, char ** argv, void * user_data);
void global_params_cleanup(global_params_t * params);

#ifndef _
#define _(x) gettext((x))
#endif


#ifdef __cplusplus
}
#endif
#endif
