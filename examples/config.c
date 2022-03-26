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

/* example - parsing a config file */
/* this example is c89 compatible */

#ifdef __TURBOC__
static void force_double(double *p) {
    double d = *p;
    force_double(&d);
}
#endif

#include <cj.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* The config data. */
typedef struct {
    /* True if tabs should be used instead of spaces. */
    CJ_BOOL use_tabs;
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
static CJ_BOOL positive_int(const CJValue *in, int *out) {
    if (in->type != CJ_NUMBER) return CJ_FALSE;
    *out = in->as.number;
    return *out >= 1;
}

/* Load the "use_tabs" property. */
static CJ_BOOL load_use_tabs(const CJValue *value, Config *out) {
    if (value->type != CJ_BOOLEAN) return CJ_FALSE;
    out->use_tabs = value->as.boolean;
    return CJ_TRUE;
}

/* Load the "indent_width" property. */
static CJ_BOOL load_indent_width(const CJValue *value, Config *out) {
    return positive_int(value, &out->indent_width);
}

/* Load the "rulers" property. */
static CJ_BOOL load_rulers(const CJValue *value, Config *out) {
    const CJArray *array;
    int *new_rulers;
    size_t i;

    if (value->type != CJ_ARRAY) return CJ_FALSE;
    array = &value->as.array;

    new_rulers = malloc(sizeof(int) * (array->length + 1));
    if (new_rulers == NULL) return CJ_FALSE;
    free(out->rulers);
    out->rulers = new_rulers;

    for (i = 0; i < array->length; ++i) {
        if (!positive_int(&array->elements[i], &out->rulers[i])) {
            return CJ_FALSE;
        }
    }
    out->rulers[array->length] = 0;

    return CJ_TRUE;
}

/* Load the "theme" property. */
static CJ_BOOL load_theme(const CJValue *value, Config *out) {
    const CJString *string;
    char *new_theme;

    if (value->type != CJ_STRING) return CJ_FALSE;
    string = &value->as.string;

    new_theme = malloc(string->length + 1);
    if (new_theme == NULL) return CJ_FALSE;

    free(out->theme);
    out->theme = new_theme;
    memcpy(out->theme, string->chars, string->length + 1);

    return CJ_TRUE;
}

static CJ_BOOL key_eq(const CJString *str, const char *test) {
    return str->length == strlen(test) && strcmp(str->chars, test) == 0;
}

/* Load the config from its object members. */
static CJ_BOOL load_config_members(const CJObject *obj, Config *out) {
    size_t i;
    for (i = 0; i < obj->length; ++i) {
        /* get object member */
        const CJObjectMember *member = &obj->members[i];
        const CJString *key = &member->key;
        const CJValue *value = &member->value;
        /* check key and set appropriate config value based on it */
        if (key_eq(key, "use_tabs")) {
            if (!load_use_tabs(value, out)) return CJ_FALSE;
        } else if (key_eq(key, "indent_width")) {
            if (!load_indent_width(value, out)) return CJ_FALSE;
        } else if (key_eq(key, "rulers")) {
            if (!load_rulers(value, out)) return CJ_FALSE;
        } else if (key_eq(key, "theme")) {
            if (!load_theme(value, out)) return CJ_FALSE;
        }
    }
    return CJ_TRUE;
}

/* Load the config from a JSON value. */
static CJ_BOOL load_config(const CJValue *in, Config *out) {
    out->use_tabs = CJ_FALSE;
    out->indent_width = 4;
    out->rulers = malloc(sizeof(int));
    out->theme = malloc(strlen(DEFAULT_THEME) + 1);
    if (out->rulers != NULL && out->theme != NULL) {
        out->rulers[0] = 0;
        memcpy(out->theme, DEFAULT_THEME, strlen(DEFAULT_THEME) + 1);
        if (in->type == CJ_OBJECT) {
            if (load_config_members(&in->as.object, out)) return CJ_TRUE;
        }
    }
    free_config(out);
    return CJ_FALSE;
}

/* Print information in the config. */
static void print_config(const Config *config) {
    int *ruler;
    printf("use tabs: %s\n", config->use_tabs ? "true" : "false");
    printf("indent width: %d\n", config->indent_width);
    printf("rulers:");
    for (ruler = config->rulers; *ruler; ++ruler) {
        printf(" %d", *ruler);
    }
    printf("\n");
    printf("theme: %s\n", config->theme);
}

int main(int argc, char *argv[]) {
    FILE *f;
    char buffer[128];
    CJFileReader file_reader;
    CJValue value;
    CJParseResult result;
    Config config;
    CJ_BOOL load_result;
    /* open config file */
    if (argc < 2) {
        fprintf(stderr, "expected config file name\n");
        return EXIT_FAILURE;
    }
    f = fopen(argv[1], "r");
    if (f == NULL) {
        fprintf(stderr, "failed to open %s: %s\n", argv[1], strerror(errno));
        return EXIT_FAILURE;
    }
    /* interfaces for building json tree */
    cj_init_file_reader(&file_reader, f, buffer, sizeof(buffer));
    /* parse json */
    result = cj_parse(NULL, &file_reader.reader, &value);
    /* close input file */
    fclose(f);
    /* handle json parsing error */
    if (result != CJ_SUCCESS) {
        fprintf(stderr, "failed to parse config file\n");
        return EXIT_FAILURE;
    }
    /* load config from json */
    load_result = load_config(&value, &config);
    /* destroy json tree */
    cj_free(NULL, &value);
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
