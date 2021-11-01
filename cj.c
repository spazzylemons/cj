/*
 * cj - a tiny and simple JSON parsing library for C
 * Copyright (c) 2021 spazzylemons
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

/* get integer type big enough for unicode codepoints (21 bits). */
#ifdef __STDC_VERSION__
#include <stdint.h>
#define Codepoint int_least32_t
#else
#define Codepoint long int
#endif

/* The parser structure. */
typedef struct {
    /* The current character, or CJ_CHAR_EOF if at EOF. */
    int current;
    /* The interfaces for reading and allocating. */
    CJAllocator *allocator;
    CJReader *reader;
    /* The depth of the parser. */
    int depth;
    /* Error handling structures. */
    jmp_buf buf;
    CJParseResult result;
} Parser;

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

static bool at_eof(const Parser *p) {
    return p->current < 0;
}

static void advance(Parser *p) {
    if (at_eof(p)) return;
    p->current = p->reader->read(p->reader);
    if (p->current == CJ_CHAR_ERR) error(p, CJ_READ_ERROR);
}

static char take_unchecked(Parser *p) {
    char result = p->current;
    advance(p);
    return result;
}

static char take(Parser *p) {
    if (at_eof(p)) error(p, CJ_SYNTAX_ERROR);
    return take_unchecked(p);
}

static void skip_ws(Parser *p) {
    for (;;) {
        switch (p->current) {
            case ' ': case '\n': case '\r': case '\t':
                advance(p);
                break;
            default:
                return;
        }
    }
}

static bool eat(Parser *p, char c) {
    if (p->current == c) {
        advance(p);
        return true;
    }
    return false;
}

static void require(Parser *p, char c) {
    if (p->current != c) error(p, CJ_SYNTAX_ERROR);
    advance(p);
}

/* starting capacity for strings, arrays, and objects */
#define INITIAL_CAPACITY 8

/*
 * A generic container generator for use with cj's container types.
 */
#define DEFINE_CONTAINER(ContainerName, func_name, ChildType)\
/* A generic container type. */ \
typedef struct {\
    size_t cap;\
    struct {\
        size_t len;\
        ChildType *ptr;\
    } *slice;\
} ContainerName;\
\
/* Initialize the container. */ \
static void func_name##_init(Parser *p, ContainerName *container, void *loc) {\
    container->slice = loc;\
    /* empty length and NULL pointer, in case of allocation error */\
    container->slice->len = 0;\
    container->slice->ptr = NULL;\
    /* then try to allocate */\
    container->slice->ptr = alloc(p, NULL, INITIAL_CAPACITY\
        * sizeof(ChildType));\
    container->cap = INITIAL_CAPACITY;\
}\
\
/* Add an element to the container. */ \
static ChildType *func_name##_add_one(Parser *p, ContainerName *container) {\
    if (container->cap == container->slice->len) {\
        /* double in size for container growth */\
        container->slice->ptr = alloc(p, container->slice->ptr,\
            container->cap * sizeof(ChildType) * 2);\
        container->cap *= 2;\
    }\
    return &container->slice->ptr[container->slice->len++];\
}\
\
/* Shrink the data to fit. */\
static void func_name##_shrink(Parser *p, ContainerName *container) {\
    if (container->slice->len == 0) {\
        /* if empty, then use NULL for the data */\
        dealloc(p->allocator, container->slice->ptr);\
        container->slice->ptr = NULL;\
    } else {\
        container->slice->ptr = alloc(p, container->slice->ptr,\
            container->slice->len * sizeof(ChildType));\
    }\
}

/* Definitions of container wrappers for all of cj's container types. */

DEFINE_CONTAINER(String, string, char)
DEFINE_CONTAINER(Array, array, CJValue)
DEFINE_CONTAINER(Object, object, CJObjectMember)

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

#define INVALID_CHAR 0xFFFD

static void push_invalid_char(Parser *p, String *string) {
    push_codepoint_unchecked(p, string, INVALID_CHAR);
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
    if (codepoint > 0x10FFFF || (codepoint > 0xD7FF && codepoint < 0xE000)) {
        codepoint = INVALID_CHAR;
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

static bool utf8_bad_cont(Parser *p, Codepoint *codepoint) {
    char c;
    if (at_eof(p)) return true;
    c = p->current;
    if (((c & 0x80) == 0) || (c & 0x40)) return true;
    *codepoint <<= 6;
    *codepoint |= c & 0x3F;
    advance(p);
    return false;
}

static Codepoint overlong_check(
    Codepoint codepoint,
    Codepoint mask,
    Codepoint min
) {
    codepoint &= mask;
    if (codepoint <= min) return INVALID_CHAR;
    return codepoint;
}

static Codepoint read_utf8_codepoint(Parser *p) {
    Codepoint codepoint;
    char b = take(p);
    if ((b & (1 << 7)) == 0) {
        if (b < ' ') error(p, CJ_SYNTAX_ERROR);
        return b;
    }
    if ((b & (1 << 6)) == 0) return INVALID_CHAR;
    codepoint = b & 0x7F;
    if (utf8_bad_cont(p, &codepoint)) return INVALID_CHAR;
    if ((b & (1 << 5)) == 0) return overlong_check(codepoint, 0x7FF, 0x7F);
    if (utf8_bad_cont(p, &codepoint)) return INVALID_CHAR;
    if ((b & (1 << 4)) == 0) return overlong_check(codepoint, 0xFFFF, 0x7FF);
    if (utf8_bad_cont(p, &codepoint)) return INVALID_CHAR;
    if ((b & (1 << 3)) == 0) return overlong_check(codepoint, 0x1FFFFF, 0xFFFF);
    /* too many bytes! */
    return INVALID_CHAR;
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
                push_codepoint(p, string, *pending | (current & 0x3FF));
            } else {
                /* print current as invalid */
                push_invalid_char(p, string);
            }
            /* nothing pending anymore */
            *pending = -1;
        } else {
            /* print pending as invalid */
            if (*pending != -1) push_invalid_char(p, string);
            /* store high half in pending */
            *pending = ((current & 0x3FF) << 10) | 0x10000;
        }
    } else {
        /* print pending as invalid */
        if (*pending != -1) push_invalid_char(p, string);
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
            push_invalid_char(p, &string);
            pending = -1;
        }
        push_codepoint(p, &string, codepoint);
    }
    if (pending != -1) push_invalid_char(p, &string);
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
    if (p->current != ']') {
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
    if (p->current != '}') {
        do {
            parse_member(p, object_add_one(p, &object));
        } while (eat(p, ','));
    }
    require(p, '}');
    object_shrink(p, &object);
}

static void push_taken(Parser *p, String *string) {
    push_char(p, string, take_unchecked(p));
}

static bool is_digit(Parser *p) {
    return p->current >= '0' && p->current <= '9';
}

static void require_digits(Parser *p, String *string) {
    if (!is_digit(p)) error(p, CJ_SYNTAX_ERROR);
    do {
        push_taken(p, string);
    } while (is_digit(p));
}

static void parse_number(Parser *p, CJValue *value) {
    /*
     * not sure if this is the best way to do it tbh
     * to parse a number, we insert a string in its place in the JSON tree
     * containing the string value of the number, then use strtod on it, and
     * finally free the string and put the number in the value.
     */
    String string;
    double number;
    value->type = CJ_STRING;
    string_init(p, &string, &value->as.string);
    /* negative sign */
    if (p->current == '-') push_taken(p, &string);
    /* integer part */
    if (p->current == '0') {
        push_taken(p, &string);
    } else {
        require_digits(p, &string);
    }
    /* fraction part */
    if (p->current == '.') {
        push_taken(p, &string);
        require_digits(p, &string);
    }
    /* exponent part */
    if (p->current == 'e' || p->current == 'E') {
        push_taken(p, &string);
        if (p->current == '-' || p->current == '+') {
            push_taken(p, &string);
        }
        require_digits(p, &string);
    }
    /* parse number */
    /* TODO - how should huge numbers (that parse to infinity) be handled? */
    push_char(p, &string, 0);
    number = strtod(value->as.string.chars, NULL);
    /* replace string with number */
    dealloc(p->allocator, value->as.string.chars);
    value->type = CJ_NUMBER;
    value->as.number = number;
}

static void parse(Parser *p, CJValue *value) {
    /* initialize to a valid state */
    value->type = CJ_NULL;
    /* check depth */
    if (++p->depth == CJ_MAX_DEPTH) {
        error(p, CJ_TOO_MUCH_NESTING);
    }
    skip_ws(p);
    if (p->current == '-' || is_digit(p)) {
        parse_number(p, value);
    } else {
        switch (take(p)) {
            case 't':
                require(p, 'r');
                require(p, 'u');
                require(p, 'e');
                value->type = CJ_BOOLEAN;
                value->as.boolean = true;
                break;
            case 'f':
                require(p, 'a');
                require(p, 'l');
                require(p, 's');
                require(p, 'e');
                value->type = CJ_BOOLEAN;
                value->as.boolean = false;
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
    p.current = ' ';
    p.allocator = allocator;
    p.reader = reader;
    p.depth = 0;
    p.result = CJ_SUCCESS;
    if (setjmp(p.buf)) {
        /* an error occurred, so free memory */
        cj_free(allocator, out);
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
