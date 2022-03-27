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

/* for error handling */
#include <setjmp.h>
/* for strtod */
#include <stdlib.h>

#ifdef CJ_STRING_READER
#include <string.h>
#endif

#ifdef __STDC_VERSION__
    /* c99 or greater */
    #include <stdint.h>
    typedef int_least32_t Codepoint;
#else
    /* c89 */
    #include <limits.h>
    #if INT_MAX >= 0x1fffff
        typedef int Codepoint;
    #elif LONG_MAX >= 0x1fffff
        typedef long Codepoint;
    #else
        /* something is very wrong with your compiler */
        #error "long is not as big as it should be"
    #endif

    #define SIZE_MAX (((size_t) 0) - 1)
#endif

/* The parser structure. */
typedef struct {
    /* The number of bytes remaining in the buffer. */
    size_t remaining;
    /* A pointer to the current character in the buffer, or NULL on EOF. */
    const char *buf_ptr;
    /* The interfaces for reading and allocating. */
    CJAllocator *allocator;
    CJReader *reader;
    /* The depth of the parser. */
    int depth;
    /* Error handling structures. */
    jmp_buf buf;
    CJParseResult result;
} Parser;

#ifdef CJ_DEFAULT_ALLOCATOR
static void *default_allocate(CJAllocator *allocator, void *ptr, size_t size) {
    (void) allocator;
    return realloc(ptr, size);
}

static CJAllocator default_allocator = { default_allocate };
#endif

#ifdef CJ_FILE_READER
static const char *file_reader_callback(CJReader *reader, size_t *size) {
    CJFileReader *file_reader = cj_container_of(reader, CJFileReader, reader);
    *size = fread(
        file_reader->buffer,
        1,
        file_reader->buffer_size,
        file_reader->file
    );
    if (*size == 0) {
        *size = !feof(file_reader->file);
        return NULL;
    }
    return file_reader->buffer;
}

void cj_init_file_reader(
    CJFileReader *file_reader,
    FILE *file,
    char *buffer,
    size_t buffer_size
) {
    file_reader->file = file;
    file_reader->buffer = buffer;
    file_reader->buffer_size = buffer_size;
    file_reader->reader.read = file_reader_callback;
}
#endif

#ifdef CJ_STRING_READER
static const char *string_reader_callback(CJReader *reader, size_t *size) {
    CJStringReader *string_reader =
        cj_container_of(reader, CJStringReader, reader);
    if (string_reader->string == NULL) {
        *size = 0;
        return NULL;
    } else {
        const char *result = string_reader->string;
        *size = string_reader->length;
        string_reader->string = NULL;
        return result;
    }
}

void cj_init_string_reader(
    CJStringReader *string_reader,
    const char *string,
    size_t length
) {
    string_reader->string = string;
    string_reader->length = length;
    string_reader->reader.read = string_reader_callback;
}
#endif

/* Global buffer for parser initial state. */
static const char initial_buffer[1] = { ' ' };

#if defined(__STDC_VERSION__)
    #if __STDC_VERSION__ >= 201112L
        #define ERROR_DECL static _Noreturn void error
    #endif
#elif defined(__clang__) || defined(__GNUC__)
    #if __has_attribute(__noreturn__)
        #define ERROR_DECL __attribute__((__noreturn__)) static void error
    #endif
#elif defined(_MSC_VER)
    #define ERROR_DECL __declspec(noreturn) static void error
#endif

#ifdef ERROR_DECL
#define UNREACHABLE do {} while (0)
#else
#define UNREACHABLE do { return 0; } while (0)
#define ERROR_DECL static void error
#endif

ERROR_DECL(Parser *p, CJParseResult result) {
    p->result = result;
    longjmp(p->buf, 1);
}

static void *alloc(Parser *p, void *ptr, size_t size) {
    void *result = p->allocator->allocate(p->allocator, ptr, size);
    /* throw on failure */
    if (result == NULL) error(p, CJ_SYNTAX_ERROR);
    return result;
}

static void dealloc(CJAllocator *allocator, void *ptr) {
    /* avoid the allocator interpreting this as an allocation */
    if (ptr == NULL) return;
    allocator->allocate(allocator, ptr, 0);
}

static CJ_BOOL at_eof(const Parser *p) {
    return p->buf_ptr == NULL;
}

static void advance(Parser *p) {
    if (at_eof(p)) return;
    if (--p->remaining == 0) {
        p->buf_ptr = p->reader->read(p->reader, &p->remaining);
        if (p->buf_ptr == NULL && p->remaining != 0) {
            error(p, CJ_READ_ERROR);
        }
    } else {
        ++p->buf_ptr;
    }
}

static char take_unchecked(Parser *p) {
    char result = *p->buf_ptr;
    advance(p);
    return result;
}

static char take(Parser *p) {
    if (at_eof(p)) error(p, CJ_SYNTAX_ERROR);
    return take_unchecked(p);
}

static void skip_ws(Parser *p) {
    while (p->buf_ptr != NULL) {
        switch (*p->buf_ptr) {
            case ' ': case '\n': case '\r': case '\t':
                advance(p);
                break;
            default:
                return;
        }
    }
}

static CJ_BOOL check(Parser *p, char c) {
    return p->buf_ptr != NULL && *p->buf_ptr == c;
}

static CJ_BOOL eat(Parser *p, char c) {
    if (check(p, c)) {
        advance(p);
        return CJ_TRUE;
    }
    return CJ_FALSE;
}

static void require(Parser *p, char c) {
    if (!check(p, c)) error(p, CJ_SYNTAX_ERROR);
    advance(p);
}

/* starting capacity for strings, arrays, and objects */
#define INITIAL_CAPACITY 8

/* A generic container. */
typedef struct {
    size_t cap;
    struct {
        size_t len;
        void *ptr;
    } *slice;
} GenericContainer;

/* Initialize the container. */
static void container_init(
    Parser *p,
    GenericContainer *container,
    void *loc,
    size_t child_size
) {
    container->slice = loc;
    /* empty length and NULL pointer, in case of allocation error */
    container->slice->len = 0;
    container->slice->ptr = NULL;
    /* then try to allocate */
    container->slice->ptr = alloc(p, NULL, INITIAL_CAPACITY * child_size);
    container->cap = INITIAL_CAPACITY;
}

/* Add an element to the container. */
static void *container_add_one(
    Parser *p,
    GenericContainer *container,
    size_t child_size
) {
    if (container->cap == container->slice->len) {
        /* guard against overflow */
        if (container->cap > SIZE_MAX / (child_size * 2)) {
            error(p, CJ_OUT_OF_MEMORY);
        }
        /* double in size for container growth */
        container->slice->ptr = alloc(p, container->slice->ptr,
            container->cap * child_size * 2);
        container->cap *= 2;
    }
    return (char*) container->slice->ptr + container->slice->len++ * child_size;
}

/* Shrink the data to fit. */
static void container_shrink(
    Parser *p,
    GenericContainer *container,
    size_t child_size
) {
    if (container->slice->len == 0) {\
        /* if empty, then use NULL for the data */
        dealloc(p->allocator, container->slice->ptr);
        container->slice->ptr = NULL;
    } else {\
        container->slice->ptr = alloc(p, container->slice->ptr,
            container->slice->len * child_size);
    }
}

/*
 * A generic container generator for use with cj's container types.
 */
#define DEFINE_CONTAINER(ContainerName, func_name, ChildType, ArrayType)\
typedef struct {\
    size_t cap;\
    ArrayType *slice;\
} ContainerName;\
\
static void func_name##_init(\
    Parser *p,\
    ContainerName *container,\
    ArrayType *loc\
) {\
    container_init(p, (void*) container, loc, sizeof(ChildType));\
}\
\
static ChildType *func_name##_add_one(Parser *p, ContainerName *container) {\
    return container_add_one(p, (void*) container, sizeof(ChildType));\
}\
\
static void func_name##_shrink(Parser *p, ContainerName *container) {\
    container_shrink(p, (void*) container, sizeof(ChildType));\
}

/* Definitions of container wrappers for all of cj's container types. */

DEFINE_CONTAINER(String, string, char, CJString)
DEFINE_CONTAINER(Array, array, CJValue, CJArray)
DEFINE_CONTAINER(Object, object, CJObjectMember, CJObject)

static void push_char(Parser *p, String *string, char c) {
    *string_add_one(p, string) = c;
}

static void push_codepoint_unchecked(
    Parser *p,
    String *string,
    Codepoint codepoint
) {
    if (codepoint < 0x80) {
        push_char(p, string, codepoint);
    } else {
        if (codepoint < 0x800) {
            push_char(p, string, 0xC0 | (codepoint >> 6));
        } else {
            if (codepoint < 0x10000) {
                push_char(p, string, 0xE0 | (codepoint >> 12));
            } else {
                push_char(p, string, 0xF0 | (codepoint >> 18));
                push_char(p, string, 0x80 | ((codepoint >> 12) & 0x3F));
            }
            push_char(p, string, 0x80 | ((codepoint >> 6) & 0x3F));
        }
        push_char(p, string, 0x80 | (codepoint & 0x3F));
    }
}

static Codepoint hex_digit(Parser *p) {
    char c = take(p);
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    error(p, CJ_SYNTAX_ERROR);
    UNREACHABLE;
}

static void push_codepoint(Parser *p, String *string, Codepoint codepoint) {
    if (codepoint > 0x10FFFF) {
        error(p, CJ_SYNTAX_ERROR);
    }
    push_codepoint_unchecked(p, string, codepoint);
}

static char read_escaped_codepoint(Parser *p) {
    char c = take(p);
    switch (c) {
        case '"': case '\\': case '/': return c;
        case 'b': return '\b';
        case 'f': return '\f';
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        default:
            error(p, CJ_SYNTAX_ERROR);
            UNREACHABLE;
    }
}

static void utf8_check_cont(Parser *p, Codepoint *codepoint) {
    char c;
    if (at_eof(p)) error(p, CJ_SYNTAX_ERROR);
    c = *p->buf_ptr;
    if (((c & 0x80) == 0) || (c & 0x40)) error(p, CJ_SYNTAX_ERROR);
    *codepoint <<= 6;
    *codepoint |= c & 0x3F;
    advance(p);
}

static Codepoint overlong_check(
    Parser *p,
    Codepoint codepoint,
    Codepoint mask,
    Codepoint min
) {
    codepoint &= mask;
    if (codepoint <= min) error(p, CJ_SYNTAX_ERROR);
    return codepoint;
}

static Codepoint read_utf8_codepoint(Parser *p) {
    Codepoint codepoint;
    char b = take(p);
    if ((b & (1 << 7)) == 0) {
        if (b < ' ') error(p, CJ_SYNTAX_ERROR);
        return b;
    }
    if ((b & (1 << 6)) == 0) error(p, CJ_SYNTAX_ERROR);
    codepoint = b & 0x7F;
    utf8_check_cont(p, &codepoint);
    if ((b & (1 << 5)) == 0) {
        return overlong_check(p, codepoint, 0x7FF, 0x7F);
    }
    utf8_check_cont(p, &codepoint);
    if ((b & (1 << 4)) == 0) {
        return overlong_check(p, codepoint, 0xFFFF, 0x7FF);
    }
    utf8_check_cont(p, &codepoint);
    if ((b & (1 << 3)) == 0) {
        return overlong_check(p, codepoint, 0x1FFFFF, 0xFFFF);
    }
    /* too many bytes! */
    error(p, CJ_SYNTAX_ERROR);
}

static void utf16_escape(Parser *p, String *string, Codepoint *pending) {
    /* read four hex digits */
    Codepoint current
        = (hex_digit(p) << 12)
        | (hex_digit(p) << 8)
        | (hex_digit(p) << 4)
        | hex_digit(p);
    /* if it's a surrogate half... */
    if ((current >> 11) == 0x1B) {
        /* low or high? */
        if (current & 0x400) {
            if (*pending != -1) {
                /* if pending high half, combine them */
                Codepoint high = ((*pending & 0x3FF) << 10) + 0x10000;
                push_codepoint(p, string, high | (current & 0x3FF));
            } else {
                /* print current */
                push_codepoint(p, string, current);
            }
            /* nothing pending anymore */
            *pending = -1;
        } else {
            /* print pending */
            if (*pending != -1) push_codepoint(p, string, *pending);
            /* store high half in pending */
            *pending = current;
        }
    } else {
        /* print pending */
        if (*pending != -1) push_codepoint(p, string, *pending);
        /* not a surrogate half, so print as is */
        push_codepoint(p, string, current);
        /* nothing pending anymore */
        *pending = -1;
    }
}

static void parse_string(Parser *p, CJString *str) {
    String string;
    Codepoint pending = -1;
    string_init(p, &string, str);
    while (!eat(p, '"')) {
        Codepoint codepoint;
        if (eat(p, '\\')) {
            if (eat(p, 'u')) {
                utf16_escape(p, &string, &pending);
                continue;
            }
            codepoint = read_escaped_codepoint(p);
        } else {
            codepoint = read_utf8_codepoint(p);
        }
        if (pending != -1) {
            push_codepoint(p, &string, pending);
            pending = -1;
        }
        push_codepoint(p, &string, codepoint);
    }
    if (pending != -1) push_codepoint(p, &string, pending);
    /* add null terminator */
    push_char(p, &string, '\0');
    string_shrink(p, &string);
    /* remove null terminator from length */
    --str->length;
}

static void parse(Parser *p, CJValue *value);

static void parse_array(Parser *p, CJArray *arr) {
    Array array;
    array_init(p, &array, arr);
    skip_ws(p);
    if (!check(p, ']')) {
        do {
            parse(p, array_add_one(p, &array));
        } while (eat(p, ','));
    }
    require(p, ']');
    array_shrink(p, &array);
}

static void parse_member(Parser *p, CJObjectMember *member) {
    /* initialize to a valid enough state in case of errors */
    member->key.chars = NULL;
    member->value.type = CJ_NULL;
    skip_ws(p);
    require(p, '"');
    parse_string(p, &member->key);
    skip_ws(p);
    require(p, ':');
    parse(p, &member->value);
}

static void parse_object(Parser *p, CJObject *obj) {
    Object object;
    object_init(p, &object, obj);
    skip_ws(p);
    if (!check(p, '}')) {
        do {
            parse_member(p, object_add_one(p, &object));
        } while (eat(p, ','));
    }
    require(p, '}');
    object_shrink(p, &object);
}

#define EXP_START 20

/* TODO handle potential overflows */
typedef struct {
    char data[26];
    int index;
    int exp;
    int exp_sign;
    int exp_add;
} NumParser;

static void init_num_parser(NumParser *np) {
    memcpy(np->data, "+0000000000000000000e+", 22);
    np->index = 1;
    np->exp = 0;
    np->exp_sign = 1;
    np->exp_add = -19;
}

static void set_sign(NumParser *np, char sign) {
    np->data[0] = sign;
}

static void add_digit(NumParser *np, char digit) {
    if (np->index == 1) {
        if (digit == '0') {
            np->exp_add--;
            return;
        }
    }
    if (np->index < EXP_START) {
        np->data[np->index++] = digit;
    }
    np->exp_add++;
}

static void add_digit_frac(NumParser *np, char digit) {
    if (np->index == 1) {
        if (digit == '0') {
            np->exp_add--;
            return;
        }
    }
    if (np->index < EXP_START) {
        np->data[np->index++] = digit;
    }
}

static int calc_exp(NumParser *np) {
    int calc = np->exp * np->exp_sign + np->exp_add;
    if (calc < -308) {
        return -999;
    } else if (calc > 308) {
        return 999;
    }
    return calc;
}

static void add_exp(NumParser *np, char digit) {
    np->exp *= 10;
    np->exp += digit - '0';
}

static void write_exp(NumParser *np) {
    int calc = calc_exp(np);
    np->data[EXP_START + 0] = 'e';
    if (calc < 0) {
        np->data[EXP_START + 1] = '-';
        calc = -calc;
    }
    np->data[EXP_START + 2] = (calc / 100) + '0';
    np->data[EXP_START + 3] = ((calc / 10) % 10) + '0';
    np->data[EXP_START + 4] = (calc % 10) + '0';
    np->data[EXP_START + 5] = '\0';
}

static CJ_BOOL is_digit(Parser *p) {
    if (at_eof(p)) return CJ_FALSE;
    return *p->buf_ptr >= '0' && *p->buf_ptr <= '9';
}

static void require_digits(
    Parser *p,
    NumParser *np,
    void (*consumer)(NumParser *np, char digit)
) {
    if (!is_digit(p)) error(p, CJ_SYNTAX_ERROR);
    do {
        consumer(np, take_unchecked(p));
    } while (is_digit(p));
}

static void parse_number(Parser *p, CJValue *value) {
    NumParser np;
    init_num_parser(&np);
    /* negative sign */
    if (check(p, '-')) {
        set_sign(&np, take_unchecked(p));
    } else {
        set_sign(&np, '+');
    }
    /* integer part */
    if (check(p, '0')) {
        advance(p);
    } else {
        require_digits(p, &np, add_digit);
    }
    /* fraction part */
    if (check(p, '.')) {
        advance(p);
        require_digits(p, &np, add_digit_frac);
    }
    /* exponent part */
    if (!at_eof(p) && (*p->buf_ptr == 'e' || *p->buf_ptr == 'E')) {
        advance(p);
        if (!eat(p, '+') && eat(p, '-')) {
            np.exp_sign = -1;
        }
        require_digits(p, &np, add_exp);
    }
    /* parse number */
    write_exp(&np);
    value->type = CJ_NUMBER;
    value->as.number = strtod(np.data, NULL);
}

static void parse(Parser *p, CJValue *value) {
    /* initialize to a valid state */
    value->type = CJ_NULL;
    /* check depth */
    if (++p->depth == CJ_MAX_DEPTH) {
        error(p, CJ_TOO_MUCH_NESTING);
    }
    skip_ws(p);
    if (check(p, '-') || is_digit(p)) {
        parse_number(p, value);
    } else {
        switch (take(p)) {
            case 't':
                require(p, 'r');
                require(p, 'u');
                require(p, 'e');
                value->type = CJ_BOOLEAN;
                value->as.boolean = CJ_TRUE;
                break;
            case 'f':
                require(p, 'a');
                require(p, 'l');
                require(p, 's');
                require(p, 'e');
                value->type = CJ_BOOLEAN;
                value->as.boolean = CJ_FALSE;
                break;
            case 'n':
                require(p, 'u');
                require(p, 'l');
                require(p, 'l');
                break;
            case '"':
                value->type = CJ_STRING;
                parse_string(p, &value->as.string);
                break;
            case '[':
                value->type = CJ_ARRAY;
                parse_array(p, &value->as.array);
                break;
            case '{':
                value->type = CJ_OBJECT;
                parse_object(p, &value->as.object);
                break;
            default:
                error(p, CJ_SYNTAX_ERROR);
        }
    }
    skip_ws(p);
    --p->depth;
}

CJParseResult cj_parse(CJAllocator *allocator, CJReader *reader, CJValue *out) {
    /* create a parser */
    Parser p;
#ifdef CJ_DEFAULT_ALLOCATOR
    if (allocator == NULL) allocator = &default_allocator;
#endif
    p.buf_ptr = initial_buffer;
    p.remaining = 1;
    p.allocator = allocator;
    p.reader = reader;
    p.depth = 0;
    p.result = CJ_SUCCESS;
    if (setjmp(p.buf)) {
        /* an error occurred, so free memory */
        cj_free(p.allocator, out);
    } else {
        /* parse root value */
        parse(&p, out);
        /* we should be at EOF, otherwise we consider it a syntax error */
        if (!at_eof(&p)) error(&p, CJ_SYNTAX_ERROR);
    }
    return p.result;
}

void cj_free(CJAllocator *allocator, const CJValue *value) {
    size_t i;
#ifdef CJ_DEFAULT_ALLOCATOR
    if (allocator == NULL) allocator = &default_allocator;
#endif
    switch (value->type) {
        case CJ_STRING:
            dealloc(allocator, value->as.string.chars);
            break;
        case CJ_ARRAY:
            for (i = 0; i < value->as.array.length; ++i) {
                cj_free(allocator, &value->as.array.elements[i]);
            }
            dealloc(allocator, value->as.array.elements);
            break;
        case CJ_OBJECT:
            for (i = 0; i < value->as.object.length; ++i) {
                CJObjectMember *member = &value->as.object.members[i];
                dealloc(allocator, member->key.chars);
                cj_free(allocator, &member->value);
            }
            dealloc(allocator, value->as.object.members);
            break;
        default:
            break;
    }
}
