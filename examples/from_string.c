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

/* example - reading from a string */
/* for simplicity, there is no error handling or type checking */

#include <cj.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *environment = "{\"FOO\":\"a\",\"BAR\":\"b\"}";

static void read_environment(const CJValue *v) {
    static char *const argv[] = {"env", NULL};

    const CJObject *obj = &v->as.object;
    char **new_env = malloc(sizeof(char*) * (obj->length + 1));

    for (size_t i = 0; i < obj->length; i++) {
        const CJObjectMember *member = &obj->members[i];
        const CJString *key = &member->key;
        const CJString *value = &member->value.as.string;

        char *variable = malloc(key->length + value->length + 2);
        sprintf(variable, "%s=%s", key->chars, value->chars);
        new_env[i] = variable;
    }

    new_env[obj->length] = NULL;

    pid_t pid = fork();
    if (pid) {
        waitpid(pid, NULL, 0);
        for (size_t i = 0; i < obj->length; i++) {
            free(new_env[i]);
        }
        free(new_env);
    } else {
        /* pass the environment to a program */
        execve("/usr/bin/env", argv, new_env);
    }
}

int main(void) {
    /* set up reader */
    CJStringReader r;
    cj_init_string_reader(&r, environment, strlen(environment));
    /* read object */
    CJValue v;
    cj_parse(NULL, &r.reader, &v);
    /* create an environment from the object */
    read_environment(&v);
    /* clean up */
    cj_free(NULL, &v);
    return 0;
}
