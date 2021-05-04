#!/bin/bash

TARGET=${1-"all"}

TARGET=$(basename "$TARGET")
TARGET=${TARGET/.[ch]/}

CC="gcc -std=gnu99 -D_DEFAULT_SOURCE -D_STAND_ALONE -g -D_DEBUG -Wall -Iinclude -Iutils "

case "${TARGET}" in
	rdb-postgres)
		${CC} -D_TEST_RDB_POSTGRES	\
			-o tests/${TARGET} 		\
			src/rdb-postgres.c 		\
			utils/*.c 				\
			$(pkg-config --cflags --libs libpq) \
			-lm -lpthread -ljson-c -lpcre
		;;
	test-psql-cursor|test-psql-bulk-insert)
		${CC} -o tests/${TARGET} tests/${TARGET}.c 	\
			src/rdb-postgres.c 						\
			utils/*.c 								\
			$(pkg-config --cflags --libs libpq) 	\
			-lm -lpthread -ljson-c -lpcre
		;;
	*)
		echo "build nothing ..."
		;;
esac



