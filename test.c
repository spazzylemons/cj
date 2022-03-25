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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* to indicate that the parse was not a success or a failure */
#define EXIT_CRASH 2

static void print_string(const CJString *str) {
    putchar('"');
    for (size_t i = 0; i < str->length; i++) {
        char c = str->chars[i];
        if ((c >= 0 && c < ' ') || c == '"' || c == '\\') {
            printf("\\u00%02x", c);
        } else {
            putchar(c);
            // printf("%c", c);
        }
    }
    putchar('"');
}

static void write_json(const CJValue *v) {
    switch (v->type) {
        case CJ_NULL:
            printf("null");
            break;
        case CJ_BOOLEAN:
            printf("%s", v->as.boolean ? "true" : "false");
            break;
        case CJ_NUMBER:
            /* infinities not yet handled by cj */
            if (v->as.number == INFINITY) {
                printf("1e309");
            } else if (v->as.number == -INFINITY) {
                printf("-1e309");
            } else {
                printf("%.17e", v->as.number);
            }
            break;
        case CJ_STRING:
            print_string(&v->as.string);
            break;
        case CJ_ARRAY:
            putchar('[');
            for (size_t i = 0; i < v->as.array.length; i++) {
                if (i > 0) putchar(',');
                write_json(&v->as.array.elements[i]);
            }
            putchar(']');
            break;
        case CJ_OBJECT:
            putchar('{');
            for (size_t i = 0; i < v->as.object.length; i++) {
                if (i > 0) putchar(',');
                print_string(&v->as.object.members[i].key);
                putchar(':');
                write_json(&v->as.object.members[i].value);
            }
            putchar('}');
            break;
    }
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
            write_json(&value);
            cj_free(NULL, &value);
            return EXIT_SUCCESS;
        case CJ_SYNTAX_ERROR: case CJ_TOO_MUCH_NESTING:
            return EXIT_FAILURE;
        case CJ_OUT_OF_MEMORY: case CJ_READ_ERROR:
            return EXIT_CRASH;
    }
}
