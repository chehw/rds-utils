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

global_params_t * global_params_init(global_params_t * params, int argc, char ** argv, void * user_data)
{
	if(NULL == params) params = calloc(1, sizeof(*params));
	assert(params);
	
	static struct option options[] = {
		{"conf", required_argument, 0, 'c' },
		{"help", no_argument, 0, 'h'},
		{NULL}
	};
	
	// try to load settings from .pgpass
	static const char * pgpass_file = ".pgpass";
	char * home_dir = getenv("HOME");
	char full_name[PATH_MAX] = "";
	snprintf(full_name, sizeof(full_name), "%s/%s", home_dir, pgpass_file);
	
	struct stat st[1];
	memset(st, 0, sizeof(st));
	int rc = stat(full_name, st);
	if(0 == rc) {
		int file_type = st->st_mode & S_IFMT;
		int file_mode = st->st_mode & 07777;
		
		if(S_ISREG(file_type) && ( (file_mode | 0600) == 0600)) {
			printf("parse pgpass_file: %s\n", full_name);
			FILE * fp = fopen(full_name, "r");
			assert(fp);
			char buf[4096] = "";
			char * line = NULL;
			while((line = fgets(buf, sizeof(buf), fp)))
			{
				if(line[0] == '\n' || line[0] == '#') continue; // skip empty lines or comment lines
				char * tok = NULL;
				char * value = NULL;
				value = strtok_r(line, ": \n", &tok);
				if(NULL == value) break;
				strncpy(params->host, value, sizeof(params->host));
				
				value = strtok_r(NULL, ": \n", &tok);
				if(NULL == value) break;
				strncpy(params->port, value, sizeof(params->port));
				
				value = strtok_r(NULL, ": \n", &tok);
				if(NULL == value) break;
				strncpy(params->dbname, value, sizeof(params->dbname));
				
				value = strtok_r(NULL, ": \n", &tok);
				if(NULL == value) break;
				strncpy(params->user, value, sizeof(params->user));
				
				value = strtok_r(NULL, ": \n", &tok);
				if(NULL == value) break;
				strncpy(params->password, value, sizeof(params->password));
			}
			
			
			fclose(fp);
		}

	}
	
	
	
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
	printf("conf_file: %s\n", conf_file);
	
	// add multi-languages support
	
	
	return params;
}

void global_params_cleanup(global_params_t * params)
{
	return;
}
