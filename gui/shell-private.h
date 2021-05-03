#ifndef PSQL_VIEWER_SHELL_PRIVATE_H_
#define PSQL_VIEWER_SHELL_PRIVATE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "shell.h"
#include <gtk/gtk.h>

typedef struct shell_private
{
	void * custom_data;
	shell_context_t * shell;
	int is_running;
	
	GtkWidget * window;
	GtkWidget * vbox;
	GtkWidget * grid;
	GtkWidget * header_bar;
	GtkWidget * statusbar;
	
	GtkWidget * address_bar;
	GtkWidget * dlg_login;
	GtkWidget * console_window;
	
	GtkWidget * stack_panel;
	GtkWidget * stack_switcher;
	
	int is_fullscreen;
	int is_maximized;
}shell_private_t;

#ifdef __cplusplus
}
#endif
#endif

