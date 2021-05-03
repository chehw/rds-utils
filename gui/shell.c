/*
 * shell.c
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

#include <sys/types.h>
#include <gtk/gtk.h>

#include "shell.h"
#include "shell-private.h"
#include "global_params.h"

shell_private_t * shell_private_new(shell_context_t * shell)
{
	shell_private_t * priv = calloc(1, sizeof(*priv));
	assert(priv);
	
	priv->shell = shell; 
	shell->priv = priv;
	return priv;
}
void shell_private_free(shell_private_t * priv)
{
	if(NULL == priv) return;
	if(priv->is_running) {
		priv->is_running = 0;
		gtk_main_quit();
	}
	free(priv);
	return;
}

static int shell_run(shell_context_t * shell) 
{
	assert(shell && shell->priv);
	shell_private_t * priv = shell->priv;
	if(priv->is_running) return 0;
	
	priv->is_running = TRUE;
	gtk_main();
	priv->is_running = FALSE;
	
	return 0;
}
static int shell_stop(shell_context_t * shell)
{
	assert(shell && shell->priv);
	shell_private_t * priv = shell->priv;
	if(priv->is_running) {
		priv->is_running = FALSE;
		gtk_main_quit();
	}
	return 0;
}

static int init_windows(shell_private_t * priv)
{
	GtkWidget * window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GtkWidget * header_bar = gtk_header_bar_new();
	GtkWidget * vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget * grid = gtk_grid_new();
	GtkWidget * statusbar = gtk_statusbar_new();
	
	gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
	gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);
	gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), _("PSQL DBViewer"));
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
	
	/* **************************************************************
	 * |				header bar									|
	 * |------------------------------------------------------------|
	 * |															|
	 * |				grid										|
	 * |															|
	 * |------------------------------------------------------------|
	 * |				statusbar									|
	** **************************************************************/
	gtk_container_add(GTK_CONTAINER(window), vbox);
	gtk_container_set_border_width(GTK_CONTAINER(window), 5);
	gtk_box_pack_start(GTK_BOX(vbox), grid, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(vbox), statusbar, FALSE, FALSE, 0);
	gtk_widget_set_hexpand(vbox, TRUE);
	gtk_widget_set_vexpand(vbox, TRUE);
	g_object_set(statusbar, "margin-start", 2, "margin-end", 2, "margin-top", 2, "margin-bottom", 2, NULL);
	
	guint msg_id = gtk_statusbar_get_context_id(GTK_STATUSBAR(statusbar), "main");
	gtk_statusbar_push(GTK_STATUSBAR(statusbar), msg_id, "status: ");
	
	priv->window = window;
	priv->vbox = vbox;
	priv->header_bar = header_bar;
	priv->grid = grid;
	priv->statusbar = statusbar;

	return 0;
}

static int shell_init(shell_context_t * shell, int (* on_shell_initialized)(shell_context_t *, void *custom_data), void * custom_data)
{
	assert(shell && shell->priv);
	shell_private_t * priv = shell->priv;
	int rc = 0;
	priv->custom_data = custom_data;
	
	rc = init_windows(priv);
	if(rc) return -1;
	
	GtkWidget * window = priv->window;
	assert(window);

	g_signal_connect_swapped(window, "destroy", G_CALLBACK(shell_stop), shell);
	
	if(on_shell_initialized) rc = on_shell_initialized(shell, custom_data);
	if(0 == rc) {
		gtk_widget_show_all(window);
	}
	return rc;
}

shell_context_t * shell_context_init(shell_context_t * shell, void * user_data)
{
	if(NULL == shell) shell = calloc(1, sizeof(* shell));
	assert(shell);
	
	shell->user_data = user_data;
	shell->init = shell_init;
	shell->run = shell_run;
	shell->stop = shell_stop;
	
	shell_private_t * priv = shell_private_new(shell);
	assert(priv && priv == shell->priv);
	
	return shell;
}

void shell_context_cleanup(shell_context_t * shell)
{
	if(NULL == shell) return;
	shell_private_free(shell->priv);
	shell->priv = NULL;
	return;
}

int shell_set_status(shell_context_t * shell, const char * fmt, ...)
{
	static const char * msg_desc = "main";
	
	assert(shell && shell->priv);
	shell_private_t * priv = shell->priv;
	GtkStatusbar * statusbar = GTK_STATUSBAR(priv->statusbar);
	assert(statusbar);
	
	char text[PATH_MAX] = "";
	if(fmt) {
		va_list ap;
		va_start(ap, fmt);
		int cb = vsnprintf(text, sizeof(text), fmt, ap);
		va_end(ap);
		if(cb <= 0) return -1;
	}
	guint msg_id = gtk_statusbar_get_context_id(statusbar, msg_desc);
	gtk_statusbar_push(statusbar, msg_id, text);
	
	return 0;
}
