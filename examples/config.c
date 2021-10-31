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

/* example - parsing a config file */

#include <cj.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* The config data. */
typedef struct {
    /* True if tabs should be used instead of spaces. */
    bool use_tabs;
    /* The width of indentation. */
    int indent_width;
    /* A list of vertical rulers, terminated with 0. */
    int *rulers;
    /* The name of the color theme. */
    char *theme;
} Config;

/* The default theme name. */
#define DEFAULT_THEME "default"

/* Free config data. */
static void free_config(Config *config) {
    free(config->rulers);
    config->rulers = NULL;
    free(config->theme);
    config->theme = NULL;
}

/* Try to get a positive integer value from a JSON value. */
static bool positive_int(const CJValue *in, int *out) {
    if (in->type != CJ_NUMBER) return false;
    *out = in->as.number;
    return *out >= 1;
}

/* Load the "use_tabs" property. */
static bool load_use_tabs(const CJValue *value, Config *out) {
    if (value->type != CJ_BOOLEAN) return false;
    out->use_tabs = value->as.boolean;
    return true;
}

/* Load the "indent_width" property. */
static bool load_indent_width(const CJValue *value, Config *out) {
    return positive_int(value, &out->indent_width);
}

/* Load the "rulers" property. */
static bool load_rulers(const CJValue *value, Config *out) {
    if (value->type != CJ_ARRAY) return false;
    const CJArray *array = &value->as.array;

    int *new_rulers = malloc(sizeof(int) * (array->length + 1));
    if (new_rulers == NULL) return false;
    free(out->rulers);
    out->rulers = new_rulers;

    for (size_t i = 0; i < array->length; ++i) {
        if (!positive_int(&array->elements[i], &out->rulers[i])) return false;
    }
    out->rulers[array->length] = 0;

    return true;
}

/* Load the "theme" property. */
static bool load_theme(const CJValue *value, Config *out) {
    if (value->type != CJ_STRING) return false;
    const CJString *string = &value->as.string;

    char *new_theme = malloc(string->length + 1);
    if (new_theme == NULL) return false;

    free(out->theme);
    out->theme = new_theme;
    memcpy(out->theme, string->chars, string->length + 1);

    return true;
}

static bool key_eq(const CJString *str, const char *test) {
    return str->length == strlen(test) && strcmp(str->chars, test) == 0;
}

/* Load the config from its object members. */
static bool load_config_members(const CJObject *obj, Config *out) {
    for (size_t i = 0; i < obj->length; ++i) {
        /* get object member */
        const CJObjectMember *member = &obj->members[i];
        const CJString *key = &member->key;
        const CJValue *value = &member->value;
        /* check key and set appropriate config value based on it */
        if (key_eq(key, "use_tabs")) {
            if (!load_use_tabs(value, out)) return false;
        } else if (key_eq(key, "indent_width")) {
            if (!load_indent_width(value, out)) return false;
        } else if (key_eq(key, "rulers")) {
            if (!load_rulers(value, out)) return false;
        } else if (key_eq(key, "theme")) {
            if (!load_theme(value, out)) return false;
        }
    }
    return true;
}

/* Load the config from a JSON value. */
static bool load_config(const CJValue *in, Config *out) {
    out->use_tabs = false;
    out->indent_width = 4;
    out->rulers = malloc(sizeof(int));
    out->theme = malloc(strlen(DEFAULT_THEME) + 1);
    if (out->rulers != NULL && out->theme != NULL) {
        out->rulers[0] = 0;
        memcpy(out->theme, DEFAULT_THEME, strlen(DEFAULT_THEME) + 1);
        if (in->type == CJ_OBJECT) {
            if (load_config_members(&in->as.object, out)) return true;
        }
    }
    free_config(out);
    return false;
}

/* Print information in the config. */
static void print_config(const Config *config) {
    printf("use tabs: %s\n", config->use_tabs ? "true" : "false");
    printf("indent width: %d\n", config->indent_width);
    printf("rulers:");
    for (int *ruler = config->rulers; *ruler; ++ruler) {
        printf(" %d", *ruler);
    }
    printf("\n");
    printf("theme: %s\n", config->theme);
}

/* Implementation of the allocation callback for cj. */
static void *allocate(CJAllocator *interface, void *ptr, size_t size) {
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

/* The file reader interface for cj, containing a file pointer. */
typedef struct {
    FILE *file;
    CJReader interface;
} FileReader;

/* Implementation of the read callback for cj. */
static int read(CJReader *interface) {
    FileReader *reader = cj_container_of(interface, FileReader, interface);
    int c = fgetc(reader->file);
    if (c == EOF) {
        if (feof(reader->file)) return CJ_CHAR_EOF;
        return CJ_CHAR_ERR;
    }
    return c;
}

int main(int argc, char *argv[]) {
    /* open config file */
    if (argc < 2) {
        fprintf(stderr, "expected config file name\n");
        return EXIT_FAILURE;
    }
    FILE *f = fopen(argv[1], "r");
    if (f == NULL) {
        fprintf(stderr, "failed to open %s: %s\n", argv[1], strerror(errno));
        return EXIT_FAILURE;
    }
    /* interfaces for building json tree */
    CJAllocator allocator = { .allocate = allocate };
    FileReader reader = { .file = f, .interface = { .read = read } };
    /* parse json */
    CJValue value;
    CJParseResult result = cj_parse(&allocator, &reader.interface, &value);
    /* close input file */
    fclose(f);
    /* handle json parsing error */
    if (result != CJ_SUCCESS) {
        fprintf(stderr, "failed to parse config file\n");
        return EXIT_FAILURE;
    }
    /* load config from json */
    Config config;
    bool load_result = load_config(&value, &config);
    /* destroy json tree */
    cj_free(&allocator, &value);
    /* handle config loading error */
    if (!load_result) {
        fprintf(stderr, "failed to load config file\n");
        return EXIT_FAILURE;
    }
    /* display config and free it */
    print_config(&config);
    free_config(&config);
    return 0;
}
