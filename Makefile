LIB_NAME := cj

OBJ_FILE := $(LIB_NAME).o
LIB_FILE := lib$(LIB_NAME).a

$(LIB_FILE): $(OBJ_FILE)
	$(AR) rcs $@ $<

# strip not required but makes it even smaller
$(OBJ_FILE): $(LIB_NAME).c $(LIB_NAME).h
	$(CC) -Os -std=c89 -Wall -Wextra -Wpedantic -Werror $(LIB_NAME).c -c -o $@

.PHONY: build clean test

build: $(LIB_FILE)

clean:
	-rm $(OBJ_FILE) $(LIB_FILE)

test: $(OBJ_FILE)
	python run_tests.py
