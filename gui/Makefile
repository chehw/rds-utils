TARGET=../bin/postgres-dbviewer

CC=gcc -std=gnu99 -D_DEFAULT_SOURCE
LINKER=$(CC)

CFLAGS = -g -Wall -I. -I../include -I../utils
LIBS = -lm -lpthread -ljson-c -lpcre

CFLAGS += $(shell pkg-config --cflags gtk+-3.0)
LIBS += $(shell pkg-config --libs gtk+-3.0)

CFLAGS += $(shell pkg-config --cflags libpq)
LIBS += $(shell pkg-config --libs libpq)

SRC_DIR=.
OBJ_DIR=../obj/gui

SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

PSQL_SOURCES := ../src/rdb-postgres.c
PSQL_OBJECTS := ../obj/rdb-postgres.o

UTILS_SRC_DIR=../utils
UTILS_OBJ_DIR=../obj/utils

UTILS_SOURCES := $(wildcard $(UTILS_SRC_DIR)/*.c)
UTILS_OBJECTS := $(UTILS_SOURCES:$(UTILS_SRC_DIR)/%.c=$(UTILS_OBJ_DIR)/%.o)

all: do_init $(TARGET)

$(TARGET): $(OBJECTS) $(PSQL_OBJECTS) $(UTILS_OBJECTS)
	$(LINKER) -o $@ $^ $(CFLAGS) $(LIBS)

$(OBJECTS): $(OBJ_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -o $@ -c $< $(CFLAGS)
	
$(PSQL_OBJECTS): ../obj/%.o : ../src/%.c
	$(CC) -o $@ -c $< $(CFLAGS)

$(UTILS_OBJECTS): $(UTILS_OBJ_DIR)/%.o : $(UTILS_SRC_DIR)/%.c
	$(CC) -o $@ -c $< $(CFLAGS)

.PHONY: do_init clean
do_init:
	mkdir -p $(OBJ_DIR) $(UTILS_OBJ_DIR) ../bin
	
clean:
	rm -f $(OBJ_DIR)/*.o $(UTILS_OBJ_DIR)/*.o $(TARGET)
