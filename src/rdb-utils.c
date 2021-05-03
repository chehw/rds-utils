/*
 * rdb-utils.c
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

enum rdb_type
{
	rdb_type_postgres,
	rdb_type_mariadb,
	rdb_type_sqllite,
	rdb_type_oracle,
	rdb_type_sqlserver,
};

typedef struct rdb_transaction
{
	void * priv;
	int (* begin)(struct rdb_transaction * trans);
	int (* save_point)(struct rdb_transaction * trans, const char * saved_name);
	int (* commit)(struct rdb_transaction * trans);
	int (* rollback)(struct rdb_transaction * trans, const char * saved_name);
}rdb_transaction_t;

typedef struct rdb_context
{
	void * user_data;
	void * priv;
	
	int (* connect)(struct rdb_context * rdb, const char * conn_string, int async_mmode);
	int (* execute)(struct rdb_context * rdb, const char * sql_statements, void ** p_result);
	void (* clear_result)(void * result);
	int (* disconnect)(struct rdb_context * rdb);
}rdb_context_t;


#if defined(_TEST_RDS_UTILS) && defined(_STAND_ALONE)
int main(int argc, char **argv)
{
	
	return 0;
}
#endif

