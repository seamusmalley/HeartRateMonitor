.PHONY: clean build

# compilation settings
CC = gcc
CFLAGS = -I$(INCLUDE_DIR) -Wall -Wextra -Werror -pedantic -std=gnu99 -pedantic -Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition -g
LDFLAGS = -lm
SQL_FLAGS = -lsqlite3

# directory paths
INCLUDE_DIR = ./include
OBJ_DIR = ./obj
SRC_DIR = ./src

# this is a list of all .h files in the include directory, separated by spaces
_DEPS = error.h histogram.h comms.h db.h

# DO NOT MODIFY DEPS
DEPS = $(patsubst %, $(INCLUDE_DIR)/%, $(_DEPS))

# this is a list of all .o files that all .c files will compile to, separated by spaces
# basically, this list should contain all names of .c files, except with a .o extension
_OBJ = main.o histogram.o comms.o db.o

# DO NOT MODIFY OBJ
OBJ = $(patsubst %, $(OBJ_DIR)/%, $(_OBJ))

# DO NOT MODIFY THIS ROLE
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $< $(SQL_FLAGS)

# DO NOT MODIFY THIS ROLE
build: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(SQL_FLAGS)

# DO NOT MODIFY THIS ROLE
clean:
	rm -f $(OBJ_DIR)/*.o
	rm -f *~
	rm -f build*
	rm -f $(INCLUDE_DIR)/*~
	rm -f histo
	rm -f a.out
	rm -f tlopv2.db

test:
	$(CC) $(CFLAGS) $(SRC_DIR)/test-db.c $(SRC_DIR)/db.c $(LDFLAGS) $(SQL_FLAGS)
