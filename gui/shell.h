#ifndef PSQL_VIEWER_SHELL_H_
#define PSQL_VIEWER_SHELL_H_

#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct shell_context
{
	void * priv;
	void * user_data;
	
	int (* init)(struct shell_context * shell, int (* on_shell_initialized)(struct shell_context *, void *custom_data), void * custom_data);
	int (* run)(struct shell_context * shell);
	int (* stop)(struct shell_context * shell);
}shell_context_t;
shell_context_t * shell_context_init(shell_context_t * shell, void * user_data);
void shell_context_cleanup(shell_context_t * shell);

int shell_set_status(shell_context_t * shell, const char * fmt, ...);

#ifdef __cplusplus
}
#endif
#endif
