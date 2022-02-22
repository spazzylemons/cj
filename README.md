# cj - a tiny JSON parsing library

cj is a tiny, flexible, and robust JSON parsing library for C.

## Building

cj is one header file and one source file. Running `make` will build an object
file and library file. Alternatively, since it is so small, you may prefer to
add the source and header files directly into your project. cj is written in C89
and so is compabtile with compilers that only support C89.

## Usage

### Parsing

To parse a JSON value using cj, simply call `cj_parse` with pointers to the
allocator, reader, and location to store the resulting value. If `cj_parse`
returns `CJ_SUCCESS`, then you may use the JSON value, and call `cj_free` with
the same allocator and value to free memory allocated for it.

```c
CJValue value;
if (cj_parse(&allocator, &reader, &value) == CJ_SUCCESS) {
    do_something(&value);
    cj_free(&allocator, &value);
} else {
    printf("failed to parse\n");
}
```

### Interfaces

cj requires you to provide interfaces for reading and allocating, and does not
provide default implementations. The interfaces contain a function pointer for
the callback, which takes the interface as the first argument. From there you
may access your own data to read from whatever data stream you desire.

```c
/* example interface - read from a file */
typedef struct {
    FILE *file;
    size_t buffer_size;
    CJReader interface;
} FileReader;

/* implement the read callback for a file reader */
static int file_reader_read(CJReader *cj_reader, size_t *size) {
    /* get the file reader from the interface pointer */
    FileReader *file_reader = cj_container_of(cj_reader, FileReader, interface);
    /* read characters, handle errors */
    *size = fread(
        cj_reader->buffer,
        1,
        file_reader->buffer_size,
        file_reader->file
    );
    if (*size == 0) {
        if (!feof(file_reader->file)) return -1;
    }
    return 0;
}

/* implement the allocate callback using only stdlib functions */
static void *basic_allocate(CJAllocator *cj_allocator, void *ptr, size_t size) {
    (void) cj_allocator;
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

/* create a buffer for read operations */
char b[128];
/* store the callback in the interface */
FileReader r = {
    .file = f,
    .buffer_size = sizeof(b),
    .interface = { .buffer = b, .read = file_reader_read },
};
/* If no data is needed for the interface, it may be used bare */
CJAllocator a = { .allocate = basic_allocate };
/* use a pointer to the interface for parsing */
CJParseResult result = cj_parse(&a, &r.interface, &value);
```

### Using the JSON data

cj is only a parsing library, and a small one at that. cj provides no methods
for traversing the JSON tree, but its structures are easy to use.

```c
/* example function - read thumbnail dimensions from json */
static bool read_dimensions(const CJValue *value, int *width, int *height) {
    /* get object from root */
    if (value->type != CJ_OBJECT) return false;
    const CJObject *obj = &value->as.object;
    /* invalid values for dimensions */
    *width = *height = 0;
    /* check members */
    for (size_t i = 0; i < obj->length; ++i) {
        /* get member */
        const CJObjectMember *member = &obj->members[i];
        /* get dimension based on key */
        int *dim;
        const CJString *key = &member->key;
        if (key->length == 5 && strcmp(key->chars, "width") == 0) {
            dim = width;
        } else if (key->length == 6 && strcmp(key->chars, "height") == 0) {
            dim = height;
        } else continue;
        /* must be number */
        if (member->value.type != CJ_NUMBER) return false;
        /* assign double to int, check overflow and that dim is positive */
        *dim = member->value.as.number;
        if (*dim < 1 || (double) *dim != member->value.as.number) return false;
    }
    /* check that both dimensions were assigned */
    return *width != 0 && *height != 0;
}

if (read_dimensions(&thumbnail_info, &width, &height)) {
    printf("thumbnail size is %dx%d\n", width, height);
}
```

### String length

Strings are null-terminated, but cj allows nulls (`'\0'`) to appear anywhere in
strings. Where possible, use the `length` field of `CJString` to specify the
length of the string.

### Encoding

cj only parses JSON encoded as UTF-8 without BOM. Invalid characters will be
converted to the replacement character "�".

### Deep nesting

In order to avoid stack overflow and other crashes, cj sets a limit for how
deeply nested a JSON object may be. This value is defined by `CJ_MAX_DEPTH`.

### Thread safety

cj is thread safe, provided your interfaces are also thread safe.

## License

cj is licensed under the MIT License.
