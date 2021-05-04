/*
 * postgres-dbviewer.c
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

#include <gtk/gtk.h>

#include "shell.h"
#include "rdb-postgres.h"

#include <limits.h>
#include <locale.h>

#include "global_params.h"
#include "shell.h"

static global_params_t g_params[1];
static shell_context_t g_shell[1];


typedef struct db_viewer_ui_context
{
	shell_context_t * shell;
	global_params_t * params;
	psql_context_t * psql;
	int is_connected;
	
	GtkWidget * btn_connect;
	GtkWidget * btn_disconnect;

	GtkWidget * hpaned;
	GtkWidget * left_panel;	// left tree
	GtkWidget * main_panel;
	
	GtkWidget * listview;
}db_viewer_ui_context_t;

static int on_shell_initialized(shell_context_t * shell, void * user_data);
int main(int argc, char **argv)
{
	setlocale(LC_ALL,"");
	
	gtk_init(&argc, &argv);
	global_params_t * params = global_params_init(g_params, argc, argv, NULL);
	assert(params);
	
	psql_context_t * psql = psql_context_init(NULL, params);
	shell_context_t * shell = shell_context_init(g_shell, params);
	
	params->shell = shell;
	params->psql = psql;
	
	db_viewer_ui_context_t ui_context[1];
	memset(ui_context, 0, sizeof(ui_context));
	ui_context->shell = shell;
	ui_context->params = params;
	ui_context->psql = psql;
	
	shell->init(shell, on_shell_initialized, ui_context);
	shell->run(shell);
	
	shell_context_cleanup(shell);
	psql_context_cleanup(psql);
	free(psql);
	global_params_cleanup(params);
	return 0;
}


#include "shell-private.h"

static void on_connect_db(GtkWidget * button, db_viewer_ui_context_t * ui_context);
static int on_shell_initialized(shell_context_t * shell, void * custom_data)
{
	assert(shell && shell->priv);
	shell_private_t * priv = shell->priv;
	
	db_viewer_ui_context_t * ui_context = custom_data;
	assert(ui_context);
	
	GtkWidget * window = priv->window;
	GtkGrid * grid = GTK_GRID(priv->grid);
	assert(grid);
	
	GtkHeaderBar * header_bar = GTK_HEADER_BAR(priv->header_bar);
	assert(header_bar);
	
	GtkWidget * button = gtk_button_new_from_icon_name("call-start", GTK_ICON_SIZE_BUTTON);
	gtk_header_bar_pack_start(header_bar, button);
	ui_context->btn_connect = button;
	g_signal_connect(button, "clicked", G_CALLBACK(on_connect_db), ui_context);
	
	button = gtk_button_new_from_icon_name("call-stop", GTK_ICON_SIZE_BUTTON);
	gtk_header_bar_pack_start(header_bar, button);
	ui_context->btn_disconnect = button;
	g_signal_connect(button, "clicked", G_CALLBACK(on_connect_db), ui_context);
	
	gtk_widget_set_sensitive(ui_context->btn_connect, !ui_context->is_connected);
	gtk_widget_set_sensitive(ui_context->btn_disconnect, ui_context->is_connected);
	
	
	gtk_window_set_default_size(GTK_WINDOW(window), 1280, 800);
	gtk_widget_show_all(window);
	gtk_widget_hide(ui_context->btn_disconnect);
	
	return 1; // do not call gtk_widget_show_all()
}

#define NUM_ENTRIES (5)
typedef union {
	struct {
		GtkEntry * host_entry;
		GtkEntry * port_entry;
		GtkEntry * dbname_entry;
		GtkEntry * user_entry;
		GtkEntry * password_entry;
	};
	GtkWidget * entries[NUM_ENTRIES];
}ui_db_connection_entries_t;
static inline void ui_connection_info_load(ui_db_connection_entries_t * items, global_params_t * params)
{
	if(params->host) gtk_entry_set_text(items->host_entry, params->host);
	if(params->port) gtk_entry_set_text(items->port_entry, params->port);
	if(params->dbname) gtk_entry_set_text(items->dbname_entry, params->dbname);
	if(params->user) gtk_entry_set_text(items->user_entry, params->user);
	if(params->password) gtk_entry_set_text(items->password_entry, params->password);
	return;
}
static inline void ui_connection_info_save(const ui_db_connection_entries_t * items, global_params_t * params)
{
	const char * host = 	gtk_entry_get_text(items->host_entry);
	const char * port = 	gtk_entry_get_text(items->port_entry);
	const char * dbname = 	gtk_entry_get_text(items->dbname_entry);
	const char * user = 	gtk_entry_get_text(items->user_entry);
	const char * password = gtk_entry_get_text(items->password_entry);
	
	if(host && host[0]) 		strncpy(params->host, host, sizeof(params->host));
	if(port && port[0]) 		strncpy(params->port, port, sizeof(params->port));
	if(dbname && dbname[0]) 	strncpy(params->dbname, dbname, sizeof(params->dbname));
	if(user && user[0]) 		strncpy(params->user, user, sizeof(params->user));
	if(password && password[0]) strncpy(params->password, password, sizeof(params->password));
	return;
}

static void ui_context_load_db(db_viewer_ui_context_t * ui_context, GtkWidget * parent_grid, psql_context_t * psql);
static GtkWidget * create_connect_db_dialog(GtkWidget * window, ui_db_connection_entries_t * items, global_params_t * params)
{
	GtkWidget * dlg = gtk_dialog_new_with_buttons("connect to ...", GTK_WINDOW(window), 
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_USE_HEADER_BAR, 
		_("Connect"), GTK_RESPONSE_APPLY,
		_("Cancel"), GTK_RESPONSE_CANCEL,
		NULL);
	assert(dlg);
	
	gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_APPLY);
	
	GtkWidget * content_area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
	gtk_container_set_border_width(GTK_CONTAINER(content_area), 5);
	GtkWidget * grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
	gtk_container_add(GTK_CONTAINER(content_area), grid);
	
	GtkWidget * label = NULL;

	const char * item_names[NUM_ENTRIES] = {
		"host",
		"port",
		"dbname",
		"user",
		"password",
	};
	
	for(int row = 0; row < NUM_ENTRIES; ++row)
	{
		label = gtk_label_new(_(item_names[row]));
		gtk_label_set_xalign(GTK_LABEL(label), 0.1);
		GtkWidget * entry = items->entries[row] = gtk_entry_new();
		gtk_widget_set_size_request(entry, 360, -1);
		gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
		gtk_grid_attach(GTK_GRID(grid), entry, 1, row, 1, 1);
		gtk_widget_set_size_request(label, 80, -1);
		gtk_widget_set_hexpand(entry, TRUE);
	}
	gtk_entry_set_invisible_char(items->password_entry, '*');
	gtk_entry_set_visibility(items->password_entry, FALSE);
	
	ui_connection_info_load(items, params);
	gtk_widget_show_all(dlg);
	
	return dlg;
}
static void on_connect_db(GtkWidget * button, db_viewer_ui_context_t * ui_context)
{
	assert(ui_context);
	global_params_t * params = ui_context->params;
	psql_context_t * psql = ui_context->psql;
	shell_context_t * shell = ui_context->shell;
	assert(shell && shell->priv);
	shell_private_t * priv = shell->priv;
	GtkWidget * window = priv->window;
	GtkWidget * header_bar = priv->header_bar;
	
	assert(window && header_bar);
	
	if(ui_context->is_connected) {	// disconnect
		psql_disconnect(psql);
		ui_context->is_connected = FALSE;
		
		gtk_widget_set_sensitive(ui_context->btn_connect, !ui_context->is_connected);
		gtk_widget_set_sensitive(ui_context->btn_disconnect, ui_context->is_connected);
		gtk_widget_hide(ui_context->btn_disconnect);
		gtk_widget_show(ui_context->btn_connect);
		
		if(ui_context->hpaned) {
			gtk_widget_destroy(ui_context->hpaned);
			ui_context->hpaned = NULL;
		}
		
		gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header_bar), "");
		shell_set_status(shell, " ");
		return;
	}
	
	// connect
	ui_db_connection_entries_t items;
	memset(&items, 0, sizeof(items));
	GtkWidget * dlg = create_connect_db_dialog(window, &items, params);
	gint response = gtk_dialog_run(GTK_DIALOG(dlg));

	if(response == GTK_RESPONSE_APPLY) {
		ui_connection_info_save(&items, params);
		char sz_conn[PATH_MAX] = "";
		snprintf(sz_conn, sizeof(sz_conn), 
			" host=%s port=%s "
			" user=%s password=%s "
			" dbname=%s ",
			params->host, params->port,
			params->user, params->password,
			params->dbname);
		int rc = psql_connect_db(psql, sz_conn, 0);	// TODO: use async-connect and check status by g_idle_add
		ui_context->is_connected = (0 == rc);
	}
	gtk_widget_destroy(dlg);
	
	gtk_widget_set_sensitive(ui_context->btn_connect, !ui_context->is_connected);
	gtk_widget_set_sensitive(ui_context->btn_disconnect, ui_context->is_connected);
	
	if(ui_context->is_connected) {
		gtk_widget_hide(ui_context->btn_connect);
		gtk_widget_show(ui_context->btn_disconnect);
		
		gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header_bar), "connected");
		ui_context_load_db(ui_context, priv->grid, psql);
	}
	
	return;
}

static void on_selection_changed_left_panel(GtkTreeSelection * selection, db_viewer_ui_context_t * ui);
static void ui_context_load_db(db_viewer_ui_context_t * ui, GtkWidget * parent_grid, psql_context_t * psql)
{
	shell_context_t * shell = ui->shell;
	// ui
	GtkWidget * hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	GtkWidget * tree = gtk_tree_view_new();
	GtkWidget * scrolled_win = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_win), GTK_SHADOW_ETCHED_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_win), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	gtk_widget_set_size_request(scrolled_win, 180, -1); 
	gtk_container_add(GTK_CONTAINER(scrolled_win), tree);
	
	// init left treeview
	GtkCellRenderer * cr = gtk_cell_renderer_text_new();
	GtkTreeViewColumn * col = gtk_tree_view_column_new_with_attributes("Name", cr, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col);
		
	cr = gtk_cell_renderer_text_new();
	col = gtk_tree_view_column_new_with_attributes("Owner", cr, "text", 1, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col);
	
	GtkTreeSelection * selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
	g_signal_connect(selection, "changed", G_CALLBACK(on_selection_changed_left_panel), ui); 
	gtk_paned_add1(GTK_PANED(hpaned), scrolled_win);
	
	// init main panel
	scrolled_win = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_win), GTK_SHADOW_ETCHED_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_win), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	
	gtk_widget_set_hexpand(scrolled_win, TRUE);
	gtk_widget_set_vexpand(scrolled_win, TRUE);
	gtk_paned_add2(GTK_PANED(hpaned), scrolled_win);
	
	gtk_paned_set_position(GTK_PANED(hpaned), 180);
	ui->hpaned = hpaned;
	ui->left_panel = tree;
	ui->main_panel = scrolled_win;
	
	gtk_grid_attach(GTK_GRID(parent_grid), hpaned, 0, 0, 1, 1);
	gtk_widget_show_all(hpaned);
	
	// load db 
	psql_result_t res = NULL;
	// show current db
	int rc = psql_execute(psql, "select current_database();", &res);
	if(0 == rc && res) {
		const char * current_db = psql_result_get_value(res, 0, 0);
		assert(current_db);
		shell_set_status(shell, "current db: %s", current_db);
	}
	psql_result_clear(&res);
	
	// list all schemas
	
	int load_schemas(db_viewer_ui_context_t * ui, GtkWidget * left_panel, psql_context_t * psql);
	rc = load_schemas(ui, tree, psql);
	assert(0 == rc);
	
	// TODO: load schemas, tables, views, ...
	
	return;
}


enum left_tree_column
{
	left_tree_column_name,
	left_tree_column_owner,
	left_tree_column_object_type,
	left_tree_columns_count
};
enum tree_item_object_type{
	tree_item_object_type_root,
	tree_item_object_type_schema,
	tree_item_object_type_sub_node,
	tree_item_object_type_table,
	tree_item_object_type_view,
	tree_item_object_types_count
};
int load_schemas(db_viewer_ui_context_t * ui, GtkWidget * left_panel, psql_context_t * psql)
{
	psql_result_t res = NULL;
	static const char * list_schemas_command = "select schema_name, schema_owner from information_schema.schemata;";
	int rc = psql_execute(psql, list_schemas_command, &res);
	if(rc || !res) {
		psql_result_clear(&res); return -1;
	}
	
	GtkWidget * tree = left_panel;
	// name, owner, object_type
	GtkTreeStore * store = gtk_tree_store_new(left_tree_columns_count, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
	GtkTreeIter root, parent;
	
	gtk_tree_store_append(store, &root, NULL);
	gtk_tree_store_set(store, &root, 
		left_tree_column_name, "schemas", 
		left_tree_column_object_type, tree_item_object_type_root,
		-1);
	
	static const char * stmt_list_tables = "list-tables";
	static const char * stmt_list_views  = "list-views";
	psql_prepare_params_t prepare_params[1] = {{
		.stmt_name = stmt_list_tables, 
		.num_params = 1,
		.types = NULL
	}};
	rc = psql_prepare(psql, "select tablename, tableowner from pg_catalog.pg_tables where schemaname=$1 ;", prepare_params);
	assert(0 == rc);
	
	prepare_params->stmt_name = stmt_list_views;
	rc = psql_prepare(psql, "select viewname, viewowner from pg_catalog.pg_views where schemaname=$1 ;", prepare_params);
	assert(0 == rc);
	
	psql_params_t query_params[1];
	psql_params_init(query_params, 1, 0);
	
	int num_rows = psql_result_get_count(res);
	for(int row = 0; row < num_rows; ++row) {
		const char * schema_name = psql_result_get_value(res, row, 0);	
		const char * owner = psql_result_get_value(res, row, 1);
		
		assert(schema_name);
		int cb_schema_name = strlen(schema_name);
		assert(cb_schema_name > 0);
		
		gtk_tree_store_append(store, &parent, &root);
		gtk_tree_store_set(store, &parent, left_tree_column_name, schema_name, 
			left_tree_column_owner, owner, 
			left_tree_column_object_type, tree_item_object_type_schema,
			-1);
		
	
		GtkTreeIter sub_node;
		GtkTreeIter child;
		gtk_tree_store_append(store, &sub_node, &parent);
		gtk_tree_store_set(store, &sub_node, 
			left_tree_column_name, "TABLES", 
			left_tree_column_object_type, tree_item_object_type_sub_node,
			-1);
		
		psql_params_setv(query_params, 1, 0, -1, schema_name, cb_schema_name, 0);
		// load tables
		psql_result_t res_tables = NULL;
		rc = psql_exec_prepared(psql, stmt_list_tables, query_params, &res_tables);
		if(0 == rc && res_tables) {
			int num_tables = psql_result_get_count(res_tables);
			for(int i = 0; i < num_tables; ++i) {
				const char * name = psql_result_get_value(res_tables, i, 0);
				const char * owner = psql_result_get_value(res_tables, i, 1);
				gtk_tree_store_append(store, &child, &sub_node);
				gtk_tree_store_set(store, &child, 
					left_tree_column_name, name, 
					left_tree_column_owner, owner, 
					left_tree_column_object_type, tree_item_object_type_table,
					-1);
			}
		}
		psql_result_clear(&res_tables);
		
		gtk_tree_store_append(store, &sub_node, &parent);
		gtk_tree_store_set(store, &sub_node, 0, "VIEWS", -1);
		// load views
		psql_result_t res_views = NULL;
		rc = psql_exec_prepared(psql, stmt_list_views, query_params, &res_views);
		if(0 == rc && res_views) {
			int num_views = psql_result_get_count(res_views);
			for(int i = 0; i < num_views; ++i) {
				const char * name = psql_result_get_value(res_views, i, 0);
				const char * owner = psql_result_get_value(res_views, i, 1);
				gtk_tree_store_append(store, &child, &sub_node);
				gtk_tree_store_set(store, &child, 
					left_tree_column_name, name, 
					left_tree_column_owner, owner, 
					left_tree_column_object_type, tree_item_object_type_view,
					-1);
			}
		}
		psql_result_clear(&res_views);
	}
	psql_params_cleanup(query_params);
	psql_result_clear(&res); 
	
	gtk_tree_view_set_model(GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
	GtkTreePath * tpath = gtk_tree_path_new_from_string("0");
	gtk_tree_view_expand_row(GTK_TREE_VIEW(tree), tpath, FALSE);
	gtk_tree_path_free(tpath);
	return 0;
}

static int ui_context_load_table_or_view(db_viewer_ui_context_t * ui, const char * schema, const char * table_name);
static void on_selection_changed_left_panel(GtkTreeSelection * selection, db_viewer_ui_context_t * ui)
{
	GtkTreeModel * model = NULL;
	GtkTreeIter iter;
	gboolean ok = gtk_tree_selection_get_selected(selection, &model, &iter);
	if(!ok || NULL == model) return;
	
	//~ GtkTreePath * path = gtk_tree_model_get_path(model, &iter);

	//~ gtk_tree_path_free(path);
	
	int type = -1;
	char * name = NULL;
	
	gtk_tree_model_get(model, &iter, 
		left_tree_column_name, &name, 
		left_tree_column_object_type, &type, 
		-1);
	
	assert(type >= 0 && type < tree_item_object_types_count);
	
	
	
	if(type == tree_item_object_type_table || type == tree_item_object_type_view) 
	{
		// find schema
		gboolean ok = FALSE;
		GtkTreeIter parent;
		GtkTreeIter sub_node;
		ok = gtk_tree_model_iter_parent(model, &sub_node, &iter);
		if(ok) ok = gtk_tree_model_iter_parent(model, &parent, &sub_node);
		if(ok) {
			char * schema = NULL;
			gtk_tree_model_get(model, &parent, 
				left_tree_column_name, &schema, 
				left_tree_column_object_type, &type, 
				-1);
			assert(type == tree_item_object_type_schema);
			ui_context_load_table_or_view(ui, schema, name);
			
		}
	}
	return;
}

static GtkWidget * ui_context_init_listview(db_viewer_ui_context_t * ui, int num_columns, const char ** col_names)
{
	GtkWidget * listview = gtk_tree_view_new();
	GtkCellRenderer * cr = NULL;
	GtkTreeViewColumn * col = NULL;
	
	assert(num_columns > 0);
	
	GType * types = calloc(num_columns, sizeof(*types));
	assert(types);
	for(int i = 0; i < num_columns; ++i) types[i] = G_TYPE_STRING;
	GtkListStore * store = gtk_list_store_newv(num_columns, types);
	
	free(types);
	for(int i = 0; i < num_columns; ++i) {
		cr = gtk_cell_renderer_text_new();
		col = gtk_tree_view_column_new_with_attributes(col_names[i], cr, "text", i, NULL);
		gtk_tree_view_column_set_resizable(col, TRUE);
		gtk_tree_view_append_column(GTK_TREE_VIEW(listview), col);
	}
	gtk_tree_view_set_model(GTK_TREE_VIEW(listview), GTK_TREE_MODEL(store));
	gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(listview), GTK_TREE_VIEW_GRID_LINES_BOTH);
	return listview;
}

static int ui_context_load_table_or_view(db_viewer_ui_context_t * ui, const char * schema, const char * table_name)
{
	assert(ui && ui->psql);
	psql_context_t * psql = ui->psql;
	
	char load_table_command[PATH_MAX] = "";
	
	snprintf(load_table_command, sizeof(load_table_command), "select * from %s.%s ;", schema, table_name);
	
	psql_result_t res = NULL;
	int rc = psql_execute(psql, load_table_command, &res);
	if(rc || !res) {
		psql_result_clear(&res);
		return -1;
	}
	
	const char ** fields = NULL;
	int num_fields = psql_result_get_fields(res, &fields);
	printf("num_fields: %d\n", num_fields);
	if(num_fields > 0) {
		GtkWidget * listview = ui_context_init_listview(ui, num_fields, fields);
		if(ui->listview) {
			gtk_widget_destroy(ui->listview);
		}
		ui->listview = listview;
		gtk_container_add(GTK_CONTAINER(ui->main_panel), listview);
		gtk_widget_show_all(listview);
	}
	
	free(fields);
	psql_result_clear(&res);
	return 0;
	
}
