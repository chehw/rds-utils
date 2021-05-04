/*
 * global_params.c
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
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>

#include <getopt.h>

#include "global_params.h"
#include "shell.h"


#include <libintl.h> // gettext


static void print_usuage(const char * exe_name)
{
	return;
}

static int parse_pg_file(const char * full_name, global_params_t * params)
{
	struct stat st[1];
	memset(st, 0, sizeof(st));
	int rc = stat(full_name, st);
	if(rc) return -1;
	
	int file_type = st->st_mode & S_IFMT;
	int file_mode = st->st_mode & 07777;
	if(!(S_ISREG(file_type) && ( (file_mode | 0600) == 0600))) return -1;
	
	
#define set_value_and_find_next(value, p, delim) ({ \
		char * p_next = NULL;						\
		p_next = strchr(p, delim);					\
		if(p_next) *p_next++ = '\0';				\
		strncpy(value, p, sizeof(value)); 			\
		p_next;										\
	})

	fprintf(stderr, "parse pgpass_file: %s\n", full_name);
	FILE * fp = fopen(full_name, "r");
	assert(fp);
	char buf[4096] = "";
	char * line = NULL;
	while((line = fgets(buf, sizeof(buf), fp)))
	{
		int cb = strlen(line);
		while(cb > 0 && (line[cb - 1] == '\n' || line[cb - 1] == '\r')) line[--cb] = '\0';
		if(0 == cb || line[0] == '#') continue; // skip empty lines or comment lines
		
		char * p = line;
		// parse host
		p = set_value_and_find_next(params->host,     p, ':'); if(NULL == p) break;
		p = set_value_and_find_next(params->port,     p, ':'); if(NULL == p) break;
		p = set_value_and_find_next(params->dbname,   p, ':'); if(NULL == p) break;
		p = set_value_and_find_next(params->user,     p, ':'); if(NULL == p) break;
		p = set_value_and_find_next(params->password, p, ':'); if(NULL == p) break;
		break;
	}
	fclose(fp);
#undef set_value_and_find_next
	return 0;
}

global_params_t * global_params_init(global_params_t * params, int argc, char ** argv, void * user_data)
{
	if(NULL == params) params = calloc(1, sizeof(*params));
	assert(params);
	
	static struct option options[] = {
		{"conf", required_argument, 0, 'c' },
		{"help", no_argument, 0, 'h'},
		{NULL}
	};
	
	static const char * default_pgpass_file = ".pgpass";
	char * home_dir = getenv("HOME");
	const char * conf_file = NULL;
	while(1) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "c:h", options, &option_index);
		if(c == -1) break;
		
		switch(c) {
		case 'c': conf_file = optarg; break;
		case 'h': 
		default:
			print_usuage(argv[0]); 
			exit(1);
		}
	}

	json_object * jconfig = NULL;
	if(conf_file) {
		fprintf(stderr, "conf_file: %s\n", conf_file);
		jconfig = json_object_from_file(conf_file);
		if(jconfig) params->jconfig = jconfig; 
	}
	
	
	// try to load settings from .pgpass
	const char * pgpass_file = default_pgpass_file;
	if(jconfig) {
		json_object * jvalue = NULL;
		json_bool ok = json_object_object_get_ex(jconfig, "pgpass_file", &jvalue);
		if(ok && jvalue) pgpass_file = json_object_get_string(jvalue);
	}
	char full_name[PATH_MAX] = "";
	snprintf(full_name, sizeof(full_name), "%s/%s", home_dir, pgpass_file);
	parse_pg_file(full_name, params);
	
	// TODO: add multi-languages support
	
	return params;
}

void global_params_cleanup(global_params_t * params)
{
	return;
}
