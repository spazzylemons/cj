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

#ifndef CJ_JSON_H
#define CJ_JSON_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { CJ_FALSE, CJ_TRUE } CJ_BOOL;

#include <stddef.h>

/* The maximum depth of a JSON object before it is rejected. */
#define CJ_MAX_DEPTH 1024

/*
 * If defined, a default allocator interface using realloc will be used if
 * NULL is passed for the allocator.
 */
#define CJ_DEFAULT_ALLOCATOR

/*
 * If defined, a built-in reader interface is available that can read from a
 * *FILE.
 */
#define CJ_FILE_READER

#ifdef CJ_FILE_READER
#include <stdio.h>
#endif

/* The type of a JSON value. */
typedef enum {
    CJ_NULL,
    CJ_BOOLEAN,
    CJ_NUMBER,
    CJ_STRING,
    CJ_ARRAY,
    CJ_OBJECT
} CJType;

/*
 * A JSON string value. The string is null-terminated, but can contain nulls, so
 * take caution.
 */
typedef struct {
    size_t length;
    char *chars;
} CJString;

/* A JSON array value. If length is 0, then elements is NULL. */
typedef struct {
    size_t length;
    struct CJValue *elements;
} CJArray;

/*
 * A JSON object value. May contain duplicate keys. If length is 0, then members
 * is NULL.
 */
typedef struct {
    size_t length;
    struct CJObjectMember *members;
} CJObject;

/* A JSON value. */
typedef struct CJValue {
    CJType type;
    union {
        CJ_BOOL  boolean;
        double   number;
        CJString string;
        CJArray  array;
        CJObject object;
    } as;
} CJValue;

/*
 * A member of a JSON object.
 */
typedef struct CJObjectMember {
    CJString key;
    CJValue value;
} CJObjectMember;

/* The result of parsing a JSON value. */
typedef enum {
    /* successful parse */
    CJ_SUCCESS,
    /* ran out of memory while parsing */
    CJ_OUT_OF_MEMORY,
    /* the JSON was invalid */
    CJ_SYNTAX_ERROR,
    /* the JSON was nested too deeply */
    CJ_TOO_MUCH_NESTING,
    /* the stream ran into an error */
    CJ_READ_ERROR
} CJParseResult;

/* The allocator interface. */
typedef struct CJAllocator {
    /*
     * If ptr is NULL, return a pointer to a newly allocated region of size
     *   bytes, or NULL if an allocation of that size cannot be made.
     * If size is 0, free ptr.
     * Otherwise, resize the allocation to the given size, moving it if
     *   necessary, and returning the location of the resized allocation, or
     *   NULL if the allocation cannot be resized.
     */
    void *(*allocate)(struct CJAllocator *allocator, void *ptr, size_t size);
} CJAllocator;

/* The reader interface. */
typedef struct CJReader {
    /* The buffer to read to. */
    char *buffer;
    /*
     * Refill the buffer. Store the number of bytes read in size on success.
     * A successful read must read at least one byte of data, or else no more
     * data will be read. On an error, return a nonzero value.
     */
    int (*read)(struct CJReader *reader, size_t *size);
} CJReader;

#ifdef CJ_FILE_READER
/* An implementation of the reader interface that reads from a file. */
typedef struct {
    CJReader reader;
    FILE *file;
    size_t buffer_size;
} CJFileReader;

/* Initialize a file reader. */
void cj_init_file_reader(
    CJFileReader *file_reader,
    FILE *file,
    char *buffer,
    size_t buffer_size
);
#endif

/* A container_of implementation for use with interfaces. */
#define cj_container_of(ptr, type, member)\
    (type*) (void*) ((char*) ptr - offsetof(type, member))

/* Try to parse a JSON value. */
CJParseResult cj_parse(CJAllocator *allocator, CJReader *reader, CJValue *out);

/* Free the memory of a JSON value. */
void cj_free(CJAllocator *allocator, const CJValue *value);

#ifdef __cplusplus
}
#endif

#endif
