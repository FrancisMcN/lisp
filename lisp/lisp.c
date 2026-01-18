#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFF_SIZE 256
#define MAX_FUNC_ARGS 64
#define INITIAL_ENV_SIZE 8
#define MAX_ENV_COUNT 1024

/**
 * Converts a string to an integer
 * @param str - the string to convert to an integer
 * @return - the parsed integer
 */
static int str_to_int(char* str) {
    return (int) strtol(str, &str, 10);
}

/* The native object types used by the interpreter */
typedef enum {NUMBER, SYMBOL, STRING, ERROR, CONS, FUNCTION, MACRO, BOOL} Type;

/* The Cons cell structure used in the interpreter */
typedef struct Cons {
    struct Object* car;
    struct Object* cdr;
} Cons;

/**
 * Environments join together forming a linked list. Environments are used to implement closures
 * and to enable tail call optimisation.
 */
typedef struct Env {
    struct Map* map;
    char marked;
    struct Env* prev;
} Env;

/**
 * A function is wrapped in this Function struct, user defined functions
 * have to be manually interpreted each time, making them less efficient.
 * Built-in functions are just a pointer to a C function. */
typedef struct Function {
    char is_user_defined;
    char is_macro;
    char rest_arg;
    struct Object* args;
    struct Object* body;
    Env* env;
    struct Object* (*fn)(Env*, struct Object** args);
} Function;

/**
 * All data used by the interpreter is an 'Object', represented
 * using the Object struct below.
 */
typedef struct Object {
    struct Object* next;
    struct Object* prev;
    char marked;
    Type type;
    union {
        int num;
        char* str;
        Cons cons;
        Function fn;
    } data;
} Object;

static Object* object_new(void);
static void object_mark(Object* obj);
static void object_free(Object* obj);
static Object* number_new(int num);
static Object* symbol_new(char* symbol);
static Object* string_new(char* str);
static Object* function_new(struct Object* (*fn)(Env* env, struct Object** args));
static Object* bool_new(char value);

static void print(Object* obj);
static Object* copy(Object* obj);
static int length(Object* obj);
static int find(Object* obj, Object* list);
static char is_type(Object* object, Type obj_type);

/**
 * Each hash table is an array of MapEntry's. The hash table needs to store
 * keys as well as values in order to resolve collisions when they happen.
 */
typedef struct MapEntry {
    char* key;
    Object* value;
} MapEntry;

/**
 * A Map is an array of MapEntry's with some extra metadata to facilitate
 * resizing the map.
 */
typedef struct Map {
    MapEntry* data;
    size_t used;
    size_t size;
} Map;

/* Pre-declaring some map / hash table function signatures */
static Map* map_new(size_t size);
static void map_put(Map* map, char* key, Object* obj);
static MapEntry map_get(Map* map, char* key);
static void map_resize(Map* map);
static void map_free(Map* map);

static Object* car(Object* obj);
static Object* cdr(Object* obj);

static Env* env_new(Env* prev) {
    Env* env;
    env = malloc(sizeof(Env));
    env->map = map_new(INITIAL_ENV_SIZE);
    env->marked = 0;
    env->prev = prev;
    return env;
}

static void env_mark(Env* env) {
    size_t j;
    Map* map;
    MapEntry* entries;

    if (env != NULL) {
        map = env->map;
        entries = map->data;

        if (!env->marked) {

            env->marked = 1;

            for (j = 0; j < map->size; j++) {
                MapEntry entry = entries[j];
                if (entry.key != NULL) {
                    object_mark(entry.value);
                }
            }

            env_mark(env->prev);

        }
    }
}

static void env_unmark(Env* env) {
    size_t j;
    Map* map;
    MapEntry* entries;
    Object* obj;

    if (env != NULL) {
        map = env->map;
        entries = map->data;

        if (env->marked) {

            env->marked = 0;

            for (j = 0; j < map->size; j++) {
                MapEntry entry = entries[j];
                if (entry.key != NULL) {
                    obj = entry.value;
                    if (is_type(obj, FUNCTION) || is_type(obj, MACRO)) {
                        env_unmark(obj->data.fn.env);
                    }
                }
            }
        }
        env_unmark(env->prev);
    }
}

static void env_put(Env* env, char* key, Object* obj) {
    map_put(env->map, key, obj);
}

static MapEntry env_get(Env* env, char* key) {
    MapEntry entry;

    entry.key = NULL;
    entry.value = NULL;
    while (env != NULL) {
        entry = map_get(env->map, key);
        if (entry.key != NULL) {
            return entry;
        }
        env = env->prev;
    }

    return entry;
}

static void env_free(Env* env) {
    if (env != NULL) {
        map_free(env->map);
        free(env);
    }
}

/**
 * GC is used to implement a mark and sweep garbage collector
 */
typedef struct GC {
    Object* tail;
    size_t objects_at_last_collection;
    size_t objects_since_last_collection;
    Env* env;
} GC;

/* Instantiate the global reference to the garbage collector,
 * I hate global variables but will allow this one. */
static GC* gc;

static GC* gc_new(Env* env) {
    GC* gc = malloc(sizeof(GC));
    gc->objects_at_last_collection = 0;
    gc->objects_since_last_collection = 0;
    gc->env = env;
    return gc;
}

/**
 * Tests the type of an object, just a helper method to
 * reduce some duplicated code.
 * @param object - the object to test
 * @param obj_type - the type to compare against
 * @return - returns true if the object is of type 'obj_type'
 */
static char is_type(Object* object, Type obj_type) {
    if (object != NULL) {
        return (char) object->type == obj_type;
    }
    return 0;
}

/**
 * Checks whether debug mode is enabled
 * @return - returns true if debug mode is enabled
 */
static char is_debug_enabled(Env* env) {
    Object* obj;
    MapEntry entry;

    entry = env_get(env, "/lisp/debug-mode");
    obj = entry.value;

    if (is_type(obj, BOOL) && obj->data.num == 1) {
        return 1;
    }
    return 0;
}

/**
 TODO: there's a bug with gc_mark/gc_sweep currently
 I think it's because I'm not un-marking environments after I'm finished with them, so their new objects
 don't get marked after the first sweep
 */
static void gc_mark(GC* gc) {

    Object* temp;
    size_t count = 0;
    size_t total = 0;

    env_mark(gc->env);

    temp = gc->tail;
    while (temp != NULL) {
        if (temp->marked) {
            count++;
        }
        total++;
        temp = temp->prev;
    }

    if (is_debug_enabled(gc->env)) {
        printf("marked %lu objects out of %lu total objects.\n", count, total);
    }
    gc->objects_at_last_collection = total;

}

static void gc_sweep(GC* gc) {

    size_t count = 0;
    
    Object* ptr = gc->tail;

    while (ptr != NULL) {
        Object* prev = ptr->prev;   /* SAVE traversal pointer FIRST */

        if (!ptr->marked) {
            /* unlink from list */
            if (ptr->prev) {
                ptr->prev->next = ptr->next;
            }
            if (ptr->next) {
                ptr->next->prev = ptr->prev;
            }

            if (ptr == gc->tail) {
                gc->tail = ptr->prev;
            }

            object_free(ptr);
            count++;
        } else {
            ptr->marked = 0;  // unmark live objects
        }

        ptr = prev;  // move safely
    }
    
    env_unmark(gc->env);

    gc->objects_since_last_collection = 0;

    if (is_debug_enabled(gc->env)) {
        printf("freed %lu objects.\n", count);
    }

}

static void gc_free(GC* gc) {
    free(gc);
}

/**
 * Creates a new Object using malloc, don't forget to free!
 * @return - returns a pointer to the heap containing the newly created object
 */
static Object* object_new(void) {
    Object* obj = (Object*)malloc(sizeof(Object));
    obj->next = NULL;
    obj->prev = NULL;
    obj->marked = 0;

    if (gc->tail == NULL) {
        gc->tail = obj;
    } else {
        gc->tail->next = obj;
        obj->prev = gc->tail;
        gc->tail = obj;
    }
    gc->objects_since_last_collection++;

    return obj;
}

/**
 * Recursively mark objects to prevent accidentally freeing them
 */
static void object_mark(Object* obj) {
    
    Function fn;
    
    if (obj != NULL) {
        obj->marked = 1;
        switch (obj->type) {
            case FUNCTION:
            case MACRO: {
                fn = obj->data.fn;
                if (obj->data.fn.is_user_defined) {
                    object_mark(fn.args);
                    object_mark(fn.body);
                    env_mark(fn.env);
                }
                break;
            }
            case CONS: {
                object_mark(car(obj));
                object_mark(cdr(obj));
                break;
            }
            default: {
                
            }
        }
    }
}

/**
 * Allocates a new number object on the heap
 * @param num - the number to store in the number object
 * @return - a reference to the number object
 */
static Object* number_new(int num) {
    Object* obj = object_new();
    obj->type = NUMBER;
    obj->data.num = num;
    return obj;
}

/**
 * Allocates a new symbol object on the heap
 * @param symbol - a string to store in the symbol object
 * @return - a reference to the symbol object
 */
static Object* symbol_new(char* symbol) {
    unsigned long len;
    Object* obj = object_new();
    obj->type = SYMBOL;

    len = strlen(symbol);
    obj->data.str = (char*)malloc(sizeof(char) * (len + 1));
    strcpy(obj->data.str, symbol);
    return obj;
}

/**
 * Allocates a new string object on the heap
 * @param str - a string to store in the string object
 * @return - a reference to the string object
 */
static Object* string_new(char* str) {
    unsigned long len;
    Object* obj = object_new();
    obj->type = STRING;

    len = strlen(str);
    obj->data.str = (char*)malloc(sizeof(char) * (len + 1));
    strcpy(obj->data.str, str);
    return obj;
}

/**
 * Allocates a new cons object on the heap
 * @param car - the first element of the cons cell
 * @param cdr - the second element of the cons cell
 * @return - the newly created cons cell
 */
static Object* cons_new(Object* car, Object* cdr) {
    Object* obj = object_new();
    obj->type = CONS;
    obj->data.cons.car = car;
    obj->data.cons.cdr = cdr;
    return obj;
}

static Object* user_defined_function_new(Env* env, Object* args, Object* body) {
    Function f;
    Object* temp;
    Object* obj = object_new();
    obj->type = FUNCTION;
    f.is_user_defined = 1;
    f.is_macro = 0;
    f.env = env_new(env);

    temp = args;
    f.rest_arg = find(symbol_new("&"), args);
    f.args = args;
    f.body = body;
    f.fn = NULL;
    obj->data.fn = f;
    return obj;
}

static Object* user_defined_macro_new(Env* env, Object* args, Object* body) {
    Function f;
    Object* obj = object_new();
    obj->type = MACRO;
    f.is_user_defined = 1;
    f.is_macro = 1;
    f.env = env_new(env);

    f.rest_arg = find(symbol_new("&"), args);
    f.args = args;
    f.body = body;
    f.fn = NULL;
    obj->data.fn = f;
    return obj;
}

static Object* function_new(struct Object* (*fn)(Env* env, struct Object** args)) {
    Function f;
    Object* obj = object_new();
    obj->type = FUNCTION;
    f.is_user_defined = 0;
    f.is_macro = 0;
    f.rest_arg = -1;
    f.args = NULL;
    f.body = NULL;
    f.env = NULL;
    f.fn = fn;
    obj->data.fn = f;
    return obj;
}

static Object* macro_new(struct Object* (*fn)(Env* env, struct Object** args)) {
    Function f;
    Object* obj = object_new();
    obj->type = MACRO;
    f.is_user_defined = 0;
    f.is_macro = 1;
    f.rest_arg = -1;
    f.args = NULL;
    f.body = NULL;
    f.fn = fn;
    obj->data.fn = f;
    return obj;
}

/**
 * Allocates a new bool object on the heap
 * @param value - a true/false value to store in the bool object
 * @return - a reference to the bool object
 */
static Object* bool_new(char value) {
    Object* obj = object_new();
    obj->type = BOOL;
    if (value) {
        obj->data.num = 1;
    } else {
        obj->data.num = 0;
    }
    return obj;
}

/**
 * Allocates a new error object on the heap
 * @param str - a string to store in the error object
 * @return - a reference to the error object
 */
static Object* error_new(char* str) {
    Object* obj;

    obj = symbol_new(str);
    obj->type = ERROR;

    return obj;
}

/**
 * Actually calls free to deallocate objects, we don't need to deallocate CONS or FUNCTION
 * sub-components directly because the CONS cells and their contents are all stored in the linked list
 * individually. Freeing CONS or FUNCTION sub-components here will cause double free's.
 * @param obj - the object to free
 */
static void object_free(Object* obj) {

    if (obj == NULL) {
        return;
    }

    switch (obj->type) {
        case NUMBER: {
            break;
        }
        case STRING: {
            free(obj->data.str);
            break;
        }
        case SYMBOL: {
            free(obj->data.str);
            break;
        }
        case ERROR: {
            free(obj->data.str);
            break;
        }
        case CONS: {
            break;
        }
        case FUNCTION: {
            break;
        }
        case MACRO: {
            break;
        }
        case BOOL: {
            break;
        }
    }

    free(obj);
}

/**
 * Reads text data from a file pointer where the data is of arbitrary length.
 * @param fp - the file pointer to read the data from
 * @return - a malloc'd string of characters of arbitrary length
 */
static char* read_string(FILE* fp) {

    size_t p = 0;
    size_t buff_size = BUFF_SIZE;

    char* buff = malloc(buff_size * sizeof(char));

    while (fp != NULL && !feof(fp) && !ferror(fp)) {

        char temp[BUFF_SIZE] = {0};
        if (fgets(temp, BUFF_SIZE, fp) != NULL) {
            memcpy(buff + p, temp, strlen(temp));
            p += strlen(temp);
            buff[p] = 0;
            /* if next line will overfill the buffer, allocate extra space */
            if (p + BUFF_SIZE > buff_size) {
                buff_size *= 2;
                buff = realloc(buff, buff_size);
            }
        }
    }

    return buff;
}

/* Start of hash table implementation */

/**
 * Initialises a new map
 * @return - returns a new, empty map
 */
static Map* map_new(size_t size) {
    size_t i;
    Map* map = (Map*)malloc(sizeof(Map));
    map->used = 0;
    map->size = size;
    map->data = malloc(sizeof(MapEntry) * size);
    
    for (i = 0; i < size; i++) {
        map->data[i].key = NULL;
        map->data[i].value = NULL;
    }
    return map;
}

/**
 * Generates a 'unique' number for a string
 * @param key - the string to hash
 * @return - a 'unique' number to represent the string
 */
static size_t hash(char* key) {
    int i;
    size_t hash = 13;
    size_t len = strlen(key);
    for (i = 0; i < len; i++) {
        hash = hash * 31 + key[i];
    }
    return hash;
}

static void map_resize(Map* map) {
    
    MapEntry* data;
    size_t existing_map_size = map->size;
    size_t new_map_size = existing_map_size * 2;
    size_t i;
    size_t size;
    size_t used;
    
    Map* new_map = map_new(new_map_size);
    for (i = 0; i < map->size; i++) {
        if (map->data[i].key != NULL) {
            map_put(new_map, map->data[i].key, map->data[i].value);
        }
    }
    
    data = new_map->data;
    size = new_map->size;
    used = new_map->used;

    free(map->data);

    map->data = data;
    map->size = size;
    map->used = used;

    free(new_map);
    
}

/**
 * Associates a key with a value in a map / hash table. Automatically resizes
 * the map / hash table as necessary, each time doubling the size of the table
 * and taking care to re-hash all existing the data.
 * @param map - the map to insert the data into
 * @param key - the key to associate with the object
 * @param obj - the object to be associated with the key
 */
static void map_put(Map* map, char* key, Object* obj) {
    char* map_key;
    size_t key_hash;
    size_t key_len = strlen(key);

    if (map->used == map->size - 1) {
        map_resize(map);
    }

    key_hash = hash(key) % (map->size);
    if (map->data[key_hash].key == NULL) {
        /* Found a free space in the hash table, store the data in the map entry */
        map_key = malloc(sizeof(char) * key_len + 1);
        strcpy(map_key, key);
        map->data[key_hash].key = map_key;
        map->data[key_hash].value = obj;
    } else {
        /* Found a collision, iterate over the hash table until we find the next
           available space */
        while (map->data[key_hash].key != NULL) {

            /* If key already exists, overwrite the value and exit */
            if (strcmp(map->data[key_hash].key, key) == 0) {
                map->data[key_hash].value = obj;
                return;
            }

            key_hash += 1;
            key_hash %= map->size;

        }
        /* Finally a free space in the table, store the data in the map entry */
        map_key = malloc(sizeof(char) * key_len + 1);
        strcpy(map_key, key);

        map->data[key_hash].key = map_key;
        map->data[key_hash].value = obj;
    }
    
    map->used++;

}

/**
 * Retrieves a value from a hash table
 * @param map - the map / hash table to do the lookup in
 * @param key - the key to search for
 * @return - the object from the map / hash table if it exists, otherwise NULL
 */
static MapEntry map_get(Map* map, char* key) {

    MapEntry entry = {NULL, NULL};

    size_t key_hash = hash(key);
    key_hash = key_hash  % (map->size);
    if (map->data[key_hash].key != NULL) {
        if (strcmp(map->data[key_hash].key, key) == 0) {
            /* Found the item, return the object */
            return map->data[key_hash];
        }

        /* Found a collision, iterate over the rest of the table to find the map entry if it exists */
        while (map->data[key_hash].key != NULL) {
            /* Found the object after the collision, return the object */
            if (strcmp(map->data[key_hash].key, key) == 0) {
                /* Found the item, return the object */
                return map->data[key_hash];
            }

            key_hash += 1;
            key_hash %= map->size;

        }

    }

    /* The value we were looking for isn't in the table */
    return entry;
}

/**
 * Deallocates the memory used by the hash table
 * @param map - the table whose memory should be deallocated
 */
static void map_free(Map* map) {
    
    size_t i;
    
    if (map != NULL) {
        
        for (i = 0; i < map->size; i++) {
            free(map->data[i].key);
        }
        
        free(map->data);
        free(map);
    }
}

/* End of hash table implementation */

Object* eval(Env* env, Object* obj);
static void init_env(Env* env);

static char is_equal(Object* a, Object* b) {
    if (a != NULL && b != NULL && a->type == b->type) {
        switch (a->type) {
            case NUMBER:
                return a->data.num == b->data.num;
            case STRING:
                return strcmp(a->data.str, b->data.str) == 0;
            case SYMBOL:
                return strcmp(a->data.str, b->data.str) == 0;
            case CONS: {
                if (is_equal(car(a), car(b))) {
                    return is_equal(cdr(a), cdr(b));
                }
                return 0;
            }
            default:
                return a->data.num == b->data.num;
        }
    }

    if (a == NULL && b == NULL) {
        return 1;
    }

    return 0;
}

static Object* type(Object* obj) {
    if (obj == NULL) {
        return string_new("nil");
    }
    switch (obj->type) {
        case NUMBER:
            return string_new("number");
        case SYMBOL:
            return string_new("symbol");
        case STRING:
            return string_new("string");
        case ERROR:
            return string_new("error");
        case FUNCTION:
            return string_new("function");
        case MACRO:
            return string_new("macro");
        case CONS:
            return string_new("cons");
        case BOOL:
            return string_new("bool");
        default:
            return string_new("unknown");
    }
}

static Object* car(Object* obj) {
    if (obj != NULL && obj->type == CONS) {
        return obj->data.cons.car;
    }
    return NULL;
}

static void setcar(Object* obj, Object* value) {
    if (obj != NULL && obj->type == CONS) {
        obj->data.cons.car = value;
    }
}

static Object* cdr(Object* obj) {
    if (obj != NULL && obj->type == CONS) {
        return obj->data.cons.cdr;
    }
    return NULL;
}

static void setcdr(Object* obj, Object* value) {
    if (obj != NULL && obj->type == CONS) {
        obj->data.cons.cdr = value;
    }
}

/**
 * Calculates the length of a list
 * @param obj - the list to determine the length of
 * @return - the length of the list
 */
static int length(Object* obj) {
    int length;
    Object* temp;

    length = 0;
    temp = obj;

    if (temp != NULL) {
        while (is_type(temp, CONS)) {
            length++;
            temp = cdr(temp);
        }
    }
    return length;
}

/**
 * Determines whether an object is contained within a list
 * @param obj - the object to find
 * @return - the position of the object or -1 if not found
 */
static int find(Object* obj, Object* list) {
    int i;
    Object* temp;

    temp = list;
    i = 0;
    while (temp != NULL) {
        if (is_equal(obj, car(temp))) {
            return i;
        }
        temp = cdr(temp);
        i++;
    }
    return -1;
}

static Object* last(Object* obj) {
    Object* last;

    last = obj;
    while (cdr(last) != NULL) {
        last = cdr(last);
    }
    return last;
}

static Object* open(Object* file_to_open) {
    char* filename;
    FILE *fp;
    Object* str;
    char* temp_string;
    char error_buff[255];

    if (file_to_open == NULL || file_to_open->type != STRING) {
        return error_new("file error: open requires 1 parameter which must be a string.");
    }

    /* Read file and evaluate each line */
    filename = file_to_open->data.str;
    fp = fopen(filename, "r");
    if (fp == NULL) {
        sprintf(error_buff, "file error: '%s' file not found", filename);
        error_buff[254] = 0;
        return error_new(error_buff);
    }

    temp_string = read_string(fp);
    str = string_new(temp_string);
    free(temp_string);
    return str;
}

static void fprint(FILE* fp, Object* obj) {
    Object* temp;

    if (obj == NULL) {
        fprintf(fp, "nil");
        return;
    }

    switch (obj->type) {
        case NUMBER: {
            fprintf(fp, "%d", obj->data.num);
            break;
        }
        case STRING:
        case ERROR:
        case SYMBOL: {
            fprintf(fp, "%s", obj->data.str);
            break;
        }
        case CONS: {
            putc('(', fp);
            temp = obj;
            while (temp != NULL) {
                if (temp->type == CONS) {
                    fprint(fp, car(temp));
                } else {
                    fprint(fp, temp);
                }
                if (cdr(temp) != NULL) {
                    putc(' ', fp);
                    if (cdr(temp)->type != CONS) {
                        putc('.', fp);
                        putc(' ', fp);
                    }
                }
                temp = cdr(temp);
            }
            putc(')', fp);
            break;
        }
        case FUNCTION:
        case MACRO: {
            fprintf(fp, "%p", (void*)&obj->data.fn.fn);
            break;
        }
        case BOOL: {
            if (obj->data.num) {
                fprintf(fp, "true");
            } else {
                fprintf(fp, "false");
            }
            break;
        }
    }
}

static void print(Object* obj) {
    fprint(stdout, obj);
}

/**
 * Reads up to 256 bytes of stdin into a buffer,
 * the buffer is passed into the function using the buff[] parameter.
 * @param buff - the buffer to store the data read from stdin
 */
static void read_stdin(char buff[]) {
    char* s = fgets(buff, BUFF_SIZE, stdin);
    if (s == NULL) {
        /* Failed to read string */
        fprintf(stderr, "something went wrong, reading string.");
        exit(EXIT_FAILURE);
    }
}

/* An enum representing each kind of token in the scanner */
typedef enum { T_LPAREN, T_RPAREN, T_STRING, T_NUMBER, T_SYMBOL, T_QUOTE, T_COMMA, T_BACKTICK, T_EOF } TokenType;

/* The struct representing a token used by the reader */
typedef struct {
    TokenType type;
    char* value;
} Token;

/**
 * Creates a new token and allocates it onto the heap
 * @param type - the token type from the TokenType enum
 * @param value - a stream of characters representing the token's value
 * @return - the newly allocated token
 */
static Token* token_new(TokenType type, char* value) {
    Token* token;
    unsigned long len;

    token = malloc(sizeof(Token));
    token->type = type;
    len = strlen(value);
    token->value = (char*)malloc(sizeof(char) * (len + 1));
    strcpy(token->value, value);

    return  token;
}

/**
 * Frees the token and its memory
 * @param token - the token to free
 */
static void token_free(Token* token) {
    if (token != NULL) {
        free(token->value);
        free(token);
    }
}

/*
 * Start of scanning and parsing functions
 */

static Object* expr(char** str);
static Object* list(char** str);
static Object* atom(char** str);

/**
 * Determines whether the provided character c is numeric
 * @param c - the character to test
 * @return true or false
 */
static char is_number(char c) {
    if (c >= '0' && c <= '9') {
        return 1;
    }
    return 0;
}

/**
 * Returns true if the character is a printable ASCII character
 * @param c - the character in question
 * @return - true if printable, false if non-printable
 */
static char is_printable(char c) {
    if (c >= 33 && c <= 126) {
        return 1;
    }
    return 0;
}

/**
 * Currently almost any character is a valid symbol
 * @param c - the character to test
 * @return true or false
 */
static char is_symbol(char c) {
    if (is_printable(c)) {
        switch (c) {
            case '(':
            case ')':
            case '\'':
            case '`':
            case ',':
                return 0;
            default:
                return 1;
        }
    }
    return 0;
}

static char is_string(char c) {
    if (c == '"') {
        return 1;
    }
    return 0;
}

/**
 * Returns the current character from the input stream
 * @param str - the input stream
 * @return - the current character in the input stream
 */
static char current_char(char** str) {
    return **str;
}

/**
 * Advances the input stream and returns the next character
 * @param str - the input stream
 * @return - the next character in the input stream
 */
static char next_char(char** str) {
    return *((*str)+1);
}

/**
 * Advances the input stream by an arbitrary amount and returns the next character
 * @param str - the input stream
 * @return - the next character in the input stream
 */
static char advance_char(char** str, size_t p) {
    (*str) += p;
    return **str;
}

/**
 * Advances the input stream until the end of the current line or EOF
 * @param str - the input stream
 */
static void scan_comment(char** str) {
    char c;
    c = current_char(str);
    while (c != '\n' && c != EOF) {
        c = advance_char(str, 1);
    }
}

/**
 * Scans a string from the input stream
 * @param str - the input stream
 * @param buff - the buffer containing the string
 */
static void scan_string(char** str, char* buff) {
    char c;
    char i;

    c = **str;
    i = 0;
    while (c != '"') {
        buff[i++] = c;
        c = *(++(*str));
    }
}

/**
 * Scans a symbol from the input stream
 * @param str - the input stream
 * @param buff - the buffer containing the symbol
 */
static void scan_symbol(char** str, char* buff) {
    char c;
    char i;

    c = **str;
    i = 0;
    while (is_symbol(c)) {
        buff[i++] = c;
        c = *(++(*str));
    }
}

/**
 * Scans a number from the input stream
 * @param str - the input stream
 * @param buff - the buffer containing the number
 */
static void scan_number(char** str, char* buff) {
    char c;
    char i;

    c = **str;
    i = 0;
    if (c == '-') {
        buff[i++] = c;
        c = *(++(*str));
    }
    while (is_number(c)) {
        buff[i++] = c;
        c = *(++(*str));
    }
}

/**
 * Determines the next token in the input stream and stores
 * the generated token in the provided buffer. Buff is allocated
 * on the stack.
 * @param str - the input stream
 * @return - how many characters of the input stream were consumed or -1 if an error occurred during scanning
 */
static Token* next(char** str) {

    char* temp;
    char consumed;
    char c;
    char buff[255] = {0};

    temp = *str;
    consumed = 0;
    c = *temp;

    while (c != '\0' && strlen(*str) > 0) {
        c = current_char(str);
        
        if (c == '(') {
            c = advance_char(str, 1);
            return token_new(T_LPAREN, "(");
        } else if (c == ')') {
            c = advance_char(str, 1);
            return token_new(T_RPAREN, ")");
        } else if (c == '\'') {
            c = advance_char(str, 1);
            return token_new(T_QUOTE, "'");
        } else if (c == ',') {
            c = advance_char(str, 1);
            return token_new(T_COMMA, ",");
        } else if (c == '`') {
            c = advance_char(str, 1);
            return token_new(T_BACKTICK, "`");
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            /* Ignore whitespace, tabs, returns and newlines */
            c = advance_char(str, 1);
        } else if (c == ';') {
            scan_comment(str);
            continue;
        } else if ((c == '-' && is_number(next_char(str))) || is_number(c)) {
            scan_number(str, buff);
            return token_new(T_NUMBER, buff);
        } else if (is_string(c)) {
            advance_char(str, 1);
            scan_string(str, buff);
            advance_char(str, 1);
            return token_new(T_STRING, buff);
        } else if (is_symbol(c)) {
            scan_symbol(str, buff);
            return token_new(T_SYMBOL, buff);
        }

    }

    return token_new(T_EOF, "EOF");
    
}

static Token* peek(char** str) {
    char* temp;
    Token* token;

    temp = *str;
    token = next(str);
    *str = temp;

    return token;
}

/**
 * Determines if the string/token in the buffer is an atom
 * @param token - the token representing the potential atom
 * @return - true or false
 */
static char is_atom(Token* token) {
    if (token->type == T_NUMBER || token->type == T_STRING || token->type == T_SYMBOL) {
        return 1;
    }
    return 0;
}

/**
 * Determines if the string/token in the buffer is an expr, i.e.
 * either the beginning of a list or an atom
 * @param token - the token representing the potential atom
 * @return - true or false
 */
static char is_expr(Token* token) {
    if (token->type == T_QUOTE || token->type == T_BACKTICK || token->type == T_COMMA || token->type == T_LPAREN || is_atom(token)) {
        return 1;
    }
    return 0;
}

/**
 * Identifies whether a cons cell represents a special form or not
 * @param cons - the cons cell to test
 * @return - true if the provided cons cell is a special form
 */
static char is_special_form(Object* cons) {
    Object* first = car(cons);
    if (is_type(first, SYMBOL)) {
        char* sym = first->data.str;
        if (strcmp(sym, "quote") == 0 ||
            strcmp(sym, "quasiquote") == 0 ||
            strcmp(sym, "eval") == 0 ||
            strcmp(sym, "define") == 0 ||
            strcmp(sym, "lambda") == 0 ||
            strcmp(sym, "macro") == 0 ||
            strcmp(sym, "do") == 0 ||
            strcmp(sym, "let") == 0 ||
            strcmp(sym, "set") == 0 ||
            strcmp(sym, "if") == 0) {
            return 1;
            }
    }
    return 0;
}

/**
 * Determines whether obj is an instance of the unquote special form
 * @param obj - the object to test
 * @return - true if obj is an instance of the unquote special form
 */
static char is_unquote(Object* obj) {
    if (is_type(obj, CONS)) {
        Object* item = car(obj);
        if (is_type(item, SYMBOL) && strcmp(item->data.str, "unquote") == 0) {
            return 1;
        }
    }
    return 0;
}

static Object* function_wrapper(Env* env, Object* function, Object* args[]) {
    
    Object* temp;
    size_t i;
    Object* res;
    
    temp = function->data.fn.args;
    
    /* Store function arguments inside local environment */
    i = 0;
    while (car(temp) != NULL) {
        Object* name = car(temp);
        env_put(env, name->data.str, args[i]);
        temp = cdr(temp);
        i++;
    }
    
    res = eval(env, function->data.fn.body);
    
    return res;
}

static Object* eval_eval_special_form(Env* env, Object* obj) {
    return eval(env, eval(env, car(cdr(obj))));
}

static Object* eval_quote_special_form(Env* env, Object* obj) {
    Object* arg;
    Object* rest;
    rest = cdr(cdr(obj));
    if (rest != NULL) {
        return error_new("quote error: only 1 argument expected.");
    }
    arg = car(cdr(obj));
    return car(cdr(obj));
}

static Object* eval_unquote(Env* env, Object* obj) {

    Object* temp = obj;

    if (is_unquote(obj)) {
        return eval(env, car(cdr(obj)));
    }

    while (is_type(temp, CONS)) {
        setcar(temp, eval_unquote(env, car(temp)));
        temp = cdr(temp);
    }

    return obj;
}

/**
 * Evaluates the quasiquote special form.
 * (quasiquote (a b c ,d) becomes (append (list (quote a)) (list (quote b)) (list (quote c)) (list d))
 * @param env - the current environment
 * @param obj - the instance of the quasiquote special form
 * @return - the evaluated special form
 */
static Object* eval_quasiquote_special_form(Env* env, Object* obj) {
    Object* temp;
    Object* cur;
    Object* res;
    Object* prev;

    temp = car(cdr(obj));
    /* soft copy the list because evaluating unquote modifies the list
     in place, so we have to soft copy the list before modifying it */
    temp = copy(temp);

    cur = cons_new(NULL, NULL);
    prev = cur;
    res = cur;

    while (temp != NULL) {

        Object* item = eval_unquote(env, car(temp));
        item = cons_new(symbol_new("quote"), cons_new(item, NULL));

        setcar(cur, cons_new(symbol_new("list"), cons_new(item, NULL)));
        setcdr(cur, cons_new(NULL, NULL));
        prev = cur;
        cur = cur->data.cons.cdr;
        temp = cdr(temp);
    }

    prev->data.cons.cdr = NULL;

    return eval(env, cons_new(symbol_new("append"), res));
}

static Object* eval_define_special_form(Env* env, Object* obj) {
    Env* root;
    Object* name = car(cdr(obj));
    Object* value = car(cdr(cdr(obj)));
    
    /* the define special form stores values in the root environment */
    root = env;
    while (root->prev != NULL) {
        root = root->prev;
    }
    env_put(root, name->data.str, eval(env, value));
    return NULL;
}

static Object* eval_let_special_form(Env* env, Object* obj) {
    Env* local_env;
    Object* temp;
    Object* let_body;
    Object* res;

    local_env = env_new(env);

    temp = car(cdr(obj));
    let_body = car(cdr(cdr(obj)));
    while (temp != NULL) {
        Object* name = car(temp);
        Object* value = eval(env, car(cdr(temp)));
        env_put(local_env, name->data.str, value);
        temp = cdr(cdr(temp));
    }

    res = eval(local_env, let_body);

    return res;
}

/**
 * The set special form has two possible variations
 * (set <var> <val>)
 * (set (<var1> <val1>) (<var2> <val2>))
 * @param env - the interpreter environment
 * @param obj - an object representing the set special form
 * @return - the result of evaluating the set special form
 */
static Object* eval_set_special_form(Env* env, Object* obj) {

    Object* temp;
    Object* name;
    Object* value;

    /* determine which version of set to evaluate */
    /* if the first argument is of type CONS then expect */
    /* to modify multiple variables */
    if (is_type(car(cdr(obj)), CONS)) {
        temp = cdr(obj);
        while (car(temp) != NULL) {
            name = car(car(temp));
            value = eval(env, car(cdr(car(temp))));
            env_put(env, name->data.str, value);
            temp = cdr(temp);
        }
    } else {
        name = car(cdr(obj));
        value = eval(env, car(cdr(cdr(obj))));
        env_put(env, name->data.str, value);
    }
    return NULL;
}

static char is_truthy(Object* obj) {
    if (obj != NULL) {
        switch (obj->type) {
            case NUMBER:
            case BOOL:
                return obj->data.num > 0;
            case ERROR:
                return 0;
            case SYMBOL:
            case STRING:
            case CONS:
            case FUNCTION:
            case MACRO:
                return 1;
        }
    }
    return 0;
}

static Object* eval_if_special_form(Env* env, Object* obj) {
    Object* cond = car(cdr(obj));
    Object* true_branch = car(cdr(cdr(obj)));
    Object* else_branch = car(cdr(cdr(cdr(obj))));
    
    Object* result = eval(env, cond);
    if (is_truthy(result)) {
        return eval(env, true_branch);
    }
    
    return eval(env, else_branch);
}

static Object* eval_lambda_special_form(Env* env, Object* obj) {
    
    Object* args = car(cdr(obj));
    Object* body = car(cdr(cdr(obj)));
    return user_defined_function_new(env, args, body);
    
}

static Object* eval_macro_special_form(Env* env, Object* obj) {
    
    Object* args = car(cdr(obj));
    Object* body = car(cdr(cdr(obj)));
    return user_defined_macro_new(env, args, body);
    
}

static Object* eval_do_special_form(Env* env, Object* obj) {
    
    Object* res = NULL;
    Object* temp = cdr(obj);
    while (car(temp) != NULL) {
        res = eval(env, car(temp));
        temp = cdr(temp);
    }
    return res;
    
}

/**
 * Evaluates the special form, uses the environment to store side effects
 * @param env - the interpreter environment
 * @param obj - an object representing the special form to be evaluated
 * @return - the result of evaluating the special form
 */
static Object* eval_special_form(Env* env, Object* obj) {
    Object* first = car(obj);
    if (strcmp(first->data.str, "quote") == 0) {
        return eval_quote_special_form(env, obj);
    }
    if (strcmp(first->data.str, "quasiquote") == 0) {
        return eval_quasiquote_special_form(env, obj);
    }
    if (strcmp(first->data.str, "eval") == 0) {
        return eval_eval_special_form(env, obj);
    }
    if (strcmp(first->data.str, "define") == 0) {
        return eval_define_special_form(env, obj);
    }
    if (strcmp(first->data.str, "lambda") == 0) {
        return eval_lambda_special_form(env, obj);
    }
    if (strcmp(first->data.str, "macro") == 0) {
        return eval_macro_special_form(env, obj);
    }
    if (strcmp(first->data.str, "do") == 0) {
        return eval_do_special_form(env, obj);
    }
    if (strcmp(first->data.str, "let") == 0) {
        return eval_let_special_form(env, obj);
    }
    if (strcmp(first->data.str, "set") == 0) {
        return eval_set_special_form(env, obj);
    }
    if (strcmp(first->data.str, "if") == 0) {
        return eval_if_special_form(env, obj);
    }
    return NULL;
}

/**
 * Evaluates a cons cell representing a function call
 * @param env - the interpreter environment
 * @param obj - the object representing a function call
 * @return - the result of evaluating the function
 */
static Object* eval_function_call(Env* env, Object* obj, char expand_macro) {

    Object* function;
    Object* temp;
    Object* args;
    Object* arg;
    Object* arg_array[MAX_FUNC_ARGS] = {0};
    Object* rest;
    Object* prev;
    Object* result;
    int i;
    int arg_count;
    int rest_arg;
    char error_buff[255] = {0};

    function = eval(env, car(obj));
    
    if (function == NULL) {
        sprintf(error_buff, "name error: function '%s' is undefined", car(obj)->data.str);
        error_buff[254] = 0;
        return error_new(error_buff);
    }

    args = cdr(obj);
    arg_count = length(args);
    rest_arg = function->data.fn.rest_arg;

    temp = args;
    i = 0;
    while (car(temp) != NULL || i < arg_count) {
        
        /* Don't evaluate args if the function is a macro */
        if (!function->data.fn.is_macro) {
            arg = eval(env, car(temp));
        } else {
            arg = car(temp);
        }

        arg_array[i++] = arg;
        temp = cdr(temp);
    }

    if (rest_arg != -1) {
        rest = cons_new(NULL, NULL);
        temp = rest;
        prev = temp;
        i = rest_arg;
        while (i >= 0 && i < arg_count) {
            setcar(temp, arg_array[i]);
            setcdr(temp, cons_new(NULL, NULL));
            arg_array[i] = NULL;
            prev = temp;
            temp = cdr(temp);
            i++;
        }
        setcdr(prev, NULL);
        arg_array[rest_arg] = rest;
    }
    
    /* Call the function */
    if (!function->data.fn.is_user_defined) {
        result = function->data.fn.fn(env_new(env), arg_array);
    } else {
        result = function_wrapper(env_new(env), function, arg_array);
    }

    /* If function was a macro then evaluate the result */
    if (expand_macro && function->data.fn.is_macro) {
        result = eval(env, result);
    }

    return result;
}

static Object* apply(Env* env, Object* args[]) {

    Object* function;
    Object* cons;
    Object* temp;
    Object* prev;
    int i;

    function = args[0];
    cons = cons_new(NULL, NULL);
    temp = cons;
    prev = temp;
    i = 1;
    while (args[i+1] != NULL) {
        setcar(temp, args[i]);
        setcdr(temp, cons_new(NULL, NULL));
        prev = temp;
        temp = cdr(temp);
        i++;
    }

    if (is_type(args[i], CONS)) {
        setcdr(prev, args[i]);
    } else {
        setcdr(prev, cons_new(args[i], NULL));
    }

    /* If the only argument passed to apply is a list the previous
     loop won't run causing the first car entry to be nil. This ugly
     fix corrects that by removing the first cons cell if its car is nil */
    if (car(cons) == NULL) {
        cons = cdr(cons);
    }

    return eval_function_call(env, cons_new(function, cons), 0);
}

/**
 * Evaluates a cons cell. In common with many lisps it first checks if it's a special form
 * and if not then assumes it's a function call.
 * @param env - the interpreter environment
 * @param obj - an object representing the list to be evaluated
 * @return - the result of evaluating the list
 */
static Object* eval_list(Env* env, Object* obj) {
    if (is_special_form(obj)) {
        return eval_special_form(env, obj);
    }

    return eval_function_call(env, obj, 1);
}

/**
 * Rewrites 'expr as (quote expr)
 * @param str - the input stream
 * @return - (quote expr)
 */
static Object* quote(char** str) {
    token_free(next(str));
    return cons_new(symbol_new("quote"), cons_new(expr(str), NULL));
}

static Object* unquote(char** str) {
    token_free(next(str));
    return cons_new(symbol_new("unquote"), cons_new(expr(str), NULL));
}

static Object* quasiquote(char** str) {
    token_free(next(str));
    return cons_new(symbol_new("quasiquote"), cons_new(expr(str), NULL));
}

/**
 * Parses a list and produces a cons object.
 * list : '(' expr* ')'
 * @param str - the input stream
 * @return - a cons object
 */
static Object* list(char** str) {

    Object* temp;
    Token* token;
    Object* prev;
    Object* obj = NULL;

    token = next(str);
    token_free(token);

    /* found an empty list which represents NULL/nil */
    token = peek(str);
    if (token->type == T_RPAREN) {
        token_free(token);
        token_free(next(str));
        return NULL;
    }

    temp = cons_new(NULL, NULL);
    obj = temp;
    prev = temp;
    while (is_expr(token)) {
        token_free(token);
        setcar(temp, expr(str));
        setcdr(temp, cons_new(NULL, NULL));
        prev = temp;
        temp = temp->data.cons.cdr;
        token = peek(str);
    }

    /* Remove extra empty cons cell, we don't need to free
     * the extra cons cell because we have garbage collection */
    prev->data.cons.cdr = NULL;

    token_free(token);
    token = next(str);
    
    if (token->type != T_RPAREN) {
        obj = error_new("syntax error: missing expected ')'");
    }
    token_free(token);
    return obj;
}

/**
 * atom : Number | String | Symbol
 */
static Object* atom(char** str) {

    Object* obj = NULL;
    Token* token = NULL;
    token = peek(str);

    /* is_number or is_number with a minus at the beginning*/
    if (token->type == T_NUMBER) {
        obj = number_new(str_to_int(token->value));
    } else if (token->type == T_STRING) {
        obj = string_new(token->value);
    } else if (token->type == T_SYMBOL) {
        obj = symbol_new(token->value);
    }
    
    token_free(token);
    
    token = next(str);
    token_free(token);

    return obj;
}

/**
 * expr : list | atom
 */
static Object* expr(char** str) {
    Token* token = NULL;
    Object* obj = NULL;

    token = peek(str);

    if (token->type == T_QUOTE) {
       /* found a shorthand quote */
        obj = quote(str);
    } else if (token->type == T_BACKTICK) {
        /* found a quasiquote */
        obj = quasiquote(str);
     } else if (token->type == T_COMMA) {
         /* found a unquote */
         obj = unquote(str);
      } else if (token->type == T_LPAREN) {
       /* found a list */
          obj = list(str);
    } else {
       /* found an atom */
        obj = atom(str);
    }
    token_free(token);
    return obj;
}

Object* parse(char** str) {
    return expr(str);
}

Object* read(char* str) {
    return parse(&str);
}

Object* eval(Env* env, Object* obj) {
    Object* res = NULL;
    MapEntry entry;
    if (obj != NULL) {
        switch (obj->type) {
            case SYMBOL: {
                /* if a symbol begins with : it's a keyword,
                 * keywords evaluate to themselves.
                 */
                if (obj->data.str[0] == ':') {
                    res = obj;
                    break;
                }
                
                entry = env_get(env, obj->data.str);
                res = entry.value;
                break;
            }
            case CONS: {
                res = eval_list(env, obj);
                break;
            }
            default: {
                res = obj;
                break;
            }
        }
    }
    return res;
}

static void exec(Env* env, char* str) {
    Object* obj;
    Object* res;

    while (strlen(str) > 0) {
        
        obj = parse(&str);

        res = eval(env, obj);
        /* Suppress nil in the REPL output,
         print errors to stderr and regular objects
         to stdout.
         */
        if (res != NULL && res->type == ERROR) {
            fprint(stderr, res);
            printf("\n");
            break;
        } else if (res != NULL) {
            print(res);
            printf("\n");
        }

        /* temporarily disabled because marking isn't working,
         * I think the issue is I'm not unmarking environments when sweep runs. So after the first sweep, objects aren't being marked correctly because their environments are already marked from
         the previous run */
        if (gc->objects_since_last_collection >= (gc->objects_at_last_collection * 1.25)) {
            gc_mark(gc);
            gc_sweep(gc);
        }

    }

}

static void exec_tests(Env* env, char* filename, char* str, size_t* pass_count, size_t* fail_count) {
    Object* first;
    Object* obj;
    size_t test_no;
    printf("=== testing (%s) ===\n", filename);
    
    test_no = 0;

    while (strlen(str) > 0) {
        
        obj = parse(&str);
        
        first = car(obj);
        if (first != NULL && strcmp(first->data.str, "deftest") == 0) {
            Object* test_name = car(cdr(obj));
            obj = eval(env, obj);
            if (obj != NULL && obj->type == BOOL && obj->data.num == 1) {
                printf("PASS ");
                (*pass_count)++;
            } else {
                printf("FAIL ");
                (*fail_count)++;
            }
            printf("%s\n", test_name->data.str);
            test_no++;

        }
        
    }
}

/*
 * End of scanning and parsing functions
 */

/**
 * Performs one expansion of a macro, if the macro expands into another macro
 * the second macro will be returned unexpanded.
 * @param macro - the macro to be expanded once
 * @return - the expanded macro
 */
static Object* macroexpand1(Env* env, Object* macro) {
    return eval_function_call(env, macro, 0);
}

/**
 * Repeatedly expand the macro until the result is no longer a macro
 * @param macro - the macro to be expanded
 * @return - the expanded macro
 */
static Object* macroexpand(Env* env, Object* macro) {
    Object* expanded = macroexpand1(env, macro);
    if (is_type(expanded, MACRO)) {
        return macroexpand(env, expanded);
    } else if (is_type(expanded, CONS)) {
        Object* expanded_car = eval(env, car(expanded));
        if (is_type(expanded_car, MACRO)) {
            return macroexpand(env, expanded);
        }
    }
    return expanded;
}

/**
 * Performs a recursive soft copy of list
 * @param obj - the object to soft copy
 * @return - the copied list
 */
static Object* copy(Object* obj) {
    Object* cons;
    Object* prev;
    Object* temp;
    Object* res;

    cons = cons_new(NULL, NULL);
    prev = cons;
    res = cons;
    temp = obj;

    while (is_type(temp, CONS)) {
        if (is_type(car(temp), CONS)) {
            setcar(cons, copy(car(temp)));
        } else {
            setcar(cons, car(temp));
        }
        setcdr(cons, cons_new(NULL, NULL));
        prev = cons;
        cons = cdr(cons);
        temp = cdr(temp);
    }
    setcdr(prev, NULL);
    return res;

}

Object* builtin_apply(Env* env, Object* args[]) {
    return apply(env, args);
}

Object* builtin_car(Env* env, Object* args[]) {
    return car(args[0]);
}

Object* builtin_setcar(Env* env, Object* args[]) {
    setcar(args[0], args[1]);
    return NULL;
}

Object* builtin_cdr(Env* env, Object* args[]) {
    return cdr(args[0]);
}

Object* builtin_setcdr(Env* env, Object* args[]) {
    setcdr(args[0], args[1]);
    return NULL;
}

Object* builtin_type(Env* env, Object* args[]) {
    return type(args[0]);
}

Object* builtin_cons(Env* env, Object* args[]) {
    return cons_new(args[0], args[1]);
}

Object* builtin_print(Env* env, Object* args[]) {
    print(args[0]);
    printf("\n");
    return NULL;
}

Object* builtin_import(Env* env, Object* args[]) {
    char* filename;
    FILE *fp;
    char* str;
    char error_buff[255];

    Object* import_path = args[0];
    if (import_path == NULL || import_path->type != STRING) {
        return error_new("import error: import requires 1 parameter which must be a string.");
    }
    
    /* Read file and evaluate each line */
    filename = import_path->data.str;
    fp = fopen(filename, "r");
    if (fp == NULL) {
        sprintf(error_buff, "import error: '%s' file not found", filename);
        error_buff[254] = 0;
        return error_new(error_buff);
    }
    
    str = read_string(fp);
    exec(env, str);
    free(str);
    return NULL;
}

Object* builtin_list(Env* env, Object* args[]) {
    size_t i = 0;
    Object* temp = cons_new(NULL, NULL);
    Object* list = temp;
    Object* prev = temp;
    while (args[i] != NULL) {
        setcar(temp, args[i]);
        setcdr(temp, cons_new(NULL, NULL));
        prev = temp;
        temp = temp->data.cons.cdr;
        i++;
    }

    /* Remove extra empty cons cell, we don't need to free
     * the extra cons cell because we have garbage collection */
    prev->data.cons.cdr = NULL;
    return list;
}

Object* builtin_read(Env* env, Object* args[]) {
    char* str = args[0]->data.str;
    Object* obj = parse(&str);
    return eval(env, obj);
    
}

Object* builtin_append(Env* env, Object* args[]) {
    
    size_t i = 0;
    Object* res = cons_new(NULL, NULL);
    
    Object* prev = res;
    Object* cur = res;
    
    char error_buff[255];

    while (args[i] != NULL) {
        Object* temp = args[i];
        if (!is_type(temp, CONS)) {
            sprintf(error_buff, "type error: append expects each argument to be a list but argument %lu is a %s.", i, type(temp)->data.str);
            error_buff[254] = 0;
            return error_new(error_buff);
        }
        while (temp != NULL) {
            setcar(cur, car(temp));
            setcdr(cur, cons_new(NULL, NULL));
            prev = cur;
            cur = cur->data.cons.cdr;
            temp = cdr(temp);
        }
        i += 1;
    }
    prev->data.cons.cdr = NULL;
    
    return res;
}

Object* builtin_error(Env* env, Object* args[]) {
    Object* arg = args[0];
    if (arg != NULL) {
        return error_new(arg->data.str);
    }
    return error_new("error");
}

Object* builtin_copy(Env* env, Object* args[]) {
    return copy(args[0]);
}

Object* builtin_len(Env* env, Object* args[]) {
    return number_new(length(args[0]));
}

Object* builtin_find(Env* env, Object* args[]) {
    int pos;
    pos = find(args[0], args[1]);
    
    if (pos > -1) {
        return number_new(pos);
    }
    return NULL;
}

Object* builtin_last(Env* env, Object* args[]) {
    return last(args[0]);
}

Object* builtin_open(Env* env, Object* args[]) {
    return open(args[0]);
}

Object* builtin_macroexpand1(Env* env, Object* args[]) {
    return macroexpand1(env, args[0]);
}

Object* builtin_macroexpand(Env* env, Object* args[]) {
    return macroexpand(env, args[0]);
}

Object* builtin_mark(Env* env, Object* args[]) {
    gc_mark(gc);
    return NULL;
}

Object* builtin_sweep(Env* env, Object* args[]) {
    gc_sweep(gc);
    return NULL;
}

static char is_greater_than(Object* a, Object* b) {
    if (a != NULL && b != NULL && a->type == b->type) {
        switch (a->type) {
            case NUMBER:
                return a->data.num > b->data.num;
            default:
                return 0;
        }
    }
    
    return 0;
}

static char is_less_than(Object* a, Object* b) {
    if (a != NULL && b != NULL && a->type == b->type) {
        switch (a->type) {
            case NUMBER:
                return a->data.num < b->data.num;
            default:
                return 0;
        }
    }
    
    return 0;
}

Object* builtin_equal(Env* env, Object* args[]) {
    
    return bool_new(is_equal(args[0], args[1]));
    
}

Object* builtin_greater_than(Env* env, Object* args[]) {
    
    return bool_new(is_greater_than(args[0], args[1]));
    
}

Object* builtin_less_than(Env* env, Object* args[]) {
    
    return bool_new(is_less_than(args[0], args[1]));
    
}

Object* builtin_plus(Env* env, Object* args[]) {

    int temp = 0;
    int i = 0;

    while (args[i] != NULL) {
        Object* arg = args[i];
        temp += arg->data.num;
        i++;
    }

    return number_new(temp);
}

Object* builtin_minus(Env* env, Object* args[]) {

    int temp = 0;
    int i = 1;
    
    temp = args[0]->data.num;

    while (args[i] != NULL) {
        Object* arg = args[i];
        temp -= arg->data.num;
        i++;
    }

    return number_new(temp);
}

Object* builtin_multiply(Env* env, Object* args[]) {

    int temp = 0;
    int i = 1;
    
    temp = args[0]->data.num;

    while (args[i] != NULL) {
        Object* arg = args[i];
        temp *= arg->data.num;
        i++;
    }

    return number_new(temp);
}

Object* builtin_divide(Env* env, Object* args[]) {

    int temp = 0;
    int i = 1;
    
    temp = args[0]->data.num;

    while (args[i] != NULL) {
        Object* arg = args[i];
        temp /= arg->data.num;
        i++;
    }

    return number_new(temp);
}

static void init_env(Env* env) {
    
    env_put(env, "nil", NULL);
    env_put(env, "true", bool_new(1));
    env_put(env, "false", bool_new(0));

    env_put(env, "apply", function_new(builtin_apply));
    env_put(env, "car", function_new(builtin_car));
    env_put(env, "setcar", function_new(builtin_setcar));
    env_put(env, "cdr", function_new(builtin_cdr));
    env_put(env, "setcdr", function_new(builtin_setcdr));
    env_put(env, "type", function_new(builtin_type));
    env_put(env, "cons", function_new(builtin_cons));
    env_put(env, "print", function_new(builtin_print));
    env_put(env, "import", function_new(builtin_import));
    env_put(env, "list", function_new(builtin_list));
    env_put(env, "read", function_new(builtin_read));
    env_put(env, "append", function_new(builtin_append));
    env_put(env, "error", function_new(builtin_error));
    env_put(env, "copy", function_new(builtin_copy));
    env_put(env, "len", function_new(builtin_len));
    env_put(env, "find", function_new(builtin_find));
    env_put(env, "last", function_new(builtin_last));
    env_put(env, "open", function_new(builtin_open));
    
    env_put(env, "macroexpand", function_new(builtin_macroexpand));
    env_put(env, "macroexpand-1", function_new(builtin_macroexpand1));

    env_put(env, "gc-mark", function_new(builtin_mark));
    env_put(env, "gc-sweep", function_new(builtin_sweep));

    env_put(env, "=", function_new(builtin_equal));
    env_put(env, ">", function_new(builtin_greater_than));
    env_put(env, "<", function_new(builtin_less_than));
    env_put(env, "+", function_new(builtin_plus));
    env_put(env, "-", function_new(builtin_minus));
    env_put(env, "*", function_new(builtin_multiply));
    env_put(env, "/", function_new(builtin_divide));
    
    exec(env, "(import \"lib/core.lisp\")");

}

/**
 * Provides a Read-Eval-Print-Loop to the Lisp interpreter.
 */
static void repl(Env* env) {

    char buff[BUFF_SIZE] = {0};

    while (strcmp(buff, "(exit)") != 0) {
        printf("> ");
        read_stdin(buff);
        exec(env, buff);
    }

}

char is_test_file(char* filename) {
    if(strlen(filename) > 10 && !strcmp(filename + strlen(filename) - 10, "_test.lisp")) {
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {

    int temp;
    size_t successful_test_count = 0;
    size_t failed_test_count = 0;
    
    Env* env = env_new(NULL);
    gc = gc_new(env);
    init_env(env);

    /* Command line arguments were provided */
    if (argc > 1) {

        /* Execute multiple files in order if they're passed in */
        for (temp = 1; temp < argc; temp++) {
            char* filename = argv[temp];
            FILE *fp = fopen(filename, "r");
            char* str = read_string(fp);
            if (!is_test_file(filename)) {
                exec(env, str);
            } else {
                exec_tests(env, filename, str, &successful_test_count, &failed_test_count);
            }
            fclose(fp);
            free(str);
        }
        if (successful_test_count > 0 || failed_test_count > 0) {
            printf("===============\n");
            printf("executed %lu tests (%lu passed, %lu failed).\n", successful_test_count + failed_test_count, successful_test_count, failed_test_count);
            printf("===============\n");
            
            if (failed_test_count > 0) {
                fprintf(stderr, "exited because tests failed!\n");
                exit(1);
            }
        }
    } else {
        repl(env);
    }

    gc_free(gc);
    env_free(env);

    return 0;
}
