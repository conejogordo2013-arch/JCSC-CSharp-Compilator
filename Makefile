CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
INCLUDES = -Iinclude
SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)

all: jccsc

jccsc: $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f src/*.o jccsc

test: jccsc
	./tests/run_tests.sh

.PHONY: all clean test
