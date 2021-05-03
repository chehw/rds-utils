#!/bin/bash

TARGET=${1-"all"}

TARGET=$(basename "$TARGET")
TARGET=${TARGET/.[ch]/}

case "${TARGET}" in
	rdb-postgres)
		gcc -std=gnu99 -D_DEFAULT_SOURCE  -D_STAND_ALONE \
			-g -Wall \
			-D_TEST_RDB_POSTGRES \
			-Iinclude -Iutils \
			-o tests/${TARGET} \
			src/rdb-postgres.c \
			utils/*.c \
			$(pkg-config --cflags --libs libpq) \
			-lm -lpthread -ljson-c -lpcre
		;;
	*)
		echo "build nothing ..."
		;;
esac



