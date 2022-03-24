/*
 * cj - a tiny and simple JSON parsing library for C
 * Copyright (c) 2022 spazzylemons
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "cj.h"

#include <stdio.h>
#include <stdlib.h>

/* to indicate that the parse was not a success or a failure */
#define EXIT_CRASH 2

/* A reader for a file. */
typedef struct {
    FILE *file;
    size_t buffer_size;
    CJReader interface;
} FileReader;

/* The implementation of the reader. */
static int read(CJReader *interface, size_t *size) {
    /* get reader struct from interface pointer */
    FileReader *reader = cj_container_of(interface, FileReader, interface);
    /* read characters, handle errors */
    *size = fread(interface->buffer, 1, reader->buffer_size, reader->file);
    if (*size == 0) {
        if (!feof(reader->file)) return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    FILE *f = fopen(argv[1], "r");
    /* ensure the input file is open */
    if (f == NULL) return EXIT_CRASH;
    /* define a buffer for the reader */
    char buffer[128];
    CJFileReader file_reader;
    cj_init_file_reader(&file_reader, f, buffer, sizeof(buffer));
    /* parse the input */
    CJValue value;
    CJParseResult result = cj_parse(NULL, &file_reader.reader, &value);
    /* close the file */
    fclose(f);
    /* handle result value */
    switch (result) {
        case CJ_SUCCESS:
            cj_free(NULL, &value);
            return EXIT_SUCCESS;
        case CJ_SYNTAX_ERROR: case CJ_TOO_MUCH_NESTING:
            return EXIT_FAILURE;
        case CJ_OUT_OF_MEMORY: case CJ_READ_ERROR:
            return EXIT_CRASH;
    }
}
