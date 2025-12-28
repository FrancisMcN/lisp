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
 * A function is wrapped in this Function struct, user defined functions
 * have to be manually interpreted each time, making them less efficient.
 * Built-in functions are just a pointer to a C function. */
typedef struct Function {
    char is_user_defined;
    char is_macro;
    char rest_arg;
    struct Object* args;
    struct Object* body;
    struct Object* (*fn)(struct Object** args);
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
static Object* function_new(struct Object* (*fn)(struct Object** args));
static Object* bool_new(char value);

static void print(Object* obj);
static Object* copy(Object* obj);
static int length(Object* obj);
static int find(Object* obj, Object* list);

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
typedef struct {
    MapEntry* data;
    size_t used;
    size_t size;
} Map;

/* Pre-declaring some map / hash table function signatures */
static Map* map_new(size_t size);
static void map_put(Map* map, char* key, Object* obj);
static Object* map_get(Map* map, char* key);
static void map_resize(Map* map);
static void map_free(Map* map);

static Object* car(Object* obj);
static Object* cdr(Object* obj);

/**
 * GC is used to implement a mark and sweep garbage collector
 */
typedef struct GC {
    Object* tail;
    size_t objects_at_last_collection;
    size_t objects_since_last_collection;
    Map* env_stack[MAX_ENV_COUNT];
    size_t tos;
} GC;

/* Instantiate the global reference to the garbage collector,
 * I hate global variables but will allow this one. */
static GC* gc;

static GC* gc_new(Map* env) {
    size_t i;
    
    GC* gc = malloc(sizeof(GC));
    gc->objects_at_last_collection = 0;
    gc->objects_since_last_collection = 0;
    for (i = 0; i < MAX_ENV_COUNT; i++) {
        gc->env_stack[i] = NULL;
    }
    gc->tos = 0;
    gc->env_stack[0] = env;
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
static char is_debug_enabled(void) {
    Object* obj = map_get(gc->env_stack[gc->tos], "/lisp/debug-mode");
    if (is_type(obj, BOOL) && obj->data.num == 1) {
        return 1;
    }
    return 0;
}

static void gc_mark(GC* gc) {

    Object* temp;
    size_t i;
    size_t j;
    size_t count = 0;
    size_t total = 0;
    for (i = 0; i < MAX_ENV_COUNT; i++) {
        
        if (gc->env_stack[i] != NULL) {
            /* Mark all objects stored in the environment map */
            MapEntry* entries = gc->env_stack[i]->data;
            for (j = 0; j < gc->env_stack[i]->size; j++) {
                MapEntry entry = entries[j];
                if (entry.key != NULL) {
                    object_mark(entry.value);
                }
            }
        }
    }

    temp = gc->tail;
    while (temp != NULL) {
        if (temp->marked) {
            count++;
        }
        total++;
        temp = temp->prev;
    }

    if (is_debug_enabled()) {
        printf("marked %lu objects out of %lu total objects.\n", count, total);
    }
    gc->objects_at_last_collection = total;

}

static void gc_sweep(GC* gc) {

    Object* temp;
    size_t count = 0;
    Object* ptr = gc->tail;

    while (ptr != NULL) {

        /* remove unmarked objects */
        if (!ptr->marked) {

            if (ptr->prev != NULL) {
                
                /* Remove object from middle or end of linked list,
                 * change the pointer in previous object to skip the object
                 * being removed. Set ptr->prev->next = ptr->next.
                 *
                 * Also, set the prev pointer in the next object to skip
                 * the object being removed. Set ptr->next->prev = ptr->prev.
                 *
                 * Now, the object has been removed from the linked list and
                 * can be safely free'd.
                 */
                ptr->prev->next = ptr->next;
                if (ptr->next != NULL) {
                    ptr->next->prev = ptr->prev;
                }
                
                /* The tail pointer also has to be updated if we free'd the last object in the list */
                if (ptr == gc->tail) {
                    gc->tail = gc->tail->prev;
                }
                
                
            } else {
                /* Remove object from start of linked list */
                gc->tail = ptr->next;
            }

            temp = ptr;
            ptr = ptr->prev;
            /* Actually free the object */
            object_free(temp);
            count++;

        } else {
            ptr->marked = 0;
            ptr = ptr->prev;
        }

    }

    gc->objects_since_last_collection = 0;

    if (is_debug_enabled()) {
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
    
    if (obj != NULL) {
        obj->marked = 1;
        switch (obj->type) {
            case FUNCTION:
            case MACRO: {
                if (obj->data.fn.is_user_defined) {
                    object_mark(obj->data.fn.args);
                    object_mark(obj->data.fn.body);
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

    len = strlen(str) - 1;
    obj->data.str = (char*)malloc(sizeof(char) * (len + 1));
    strcpy(obj->data.str, str + 1);
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

static Object* user_defined_function_new(Object* args, Object* body) {
    Function f;
    Object* temp;
    Object* obj = object_new();
    obj->type = FUNCTION;
    f.is_user_defined = 1;
    f.is_macro = 0;

    temp = args;
    f.rest_arg = find(symbol_new("&"), args);
    f.args = args;
    f.body = body;
    f.fn = NULL;
    obj->data.fn = f;
    return obj;
}

static Object* user_defined_macro_new(Object* args, Object* body) {
    Function f;
    Object* obj = object_new();
    obj->type = MACRO;
    f.is_user_defined = 1;
    f.is_macro = 1;

    f.rest_arg = find(symbol_new("&"), args);
    f.args = args;
    f.body = body;
    f.fn = NULL;
    obj->data.fn = f;
    return obj;
}

static Object* function_new(struct Object* (*fn)(struct Object** args)) {
    Function f;
    Object* obj = object_new();
    obj->type = FUNCTION;
    f.is_user_defined = 0;
    f.is_macro = 0;
    f.rest_arg = -1;
    f.args = NULL;
    f.body = NULL;
    f.fn = fn;
    obj->data.fn = f;
    return obj;
}

static Object* macro_new(struct Object* (*fn)(struct Object** args)) {
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
    
    size_t existing_map_size = map->size;
    size_t new_map_size = existing_map_size * 2;
    size_t i;
    
    Map* new_map = map_new(new_map_size);
    for (i = 0; i < map->size; i++) {
        if (map->data[i].key != NULL) {
            map_put(new_map, map->data[i].key, map->data[i].value);
        }
    }
    
    MapEntry* data = new_map->data;
    size_t size = new_map->size;
    size_t used = new_map->used;

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
        char* map_key = malloc(sizeof(char) * key_len + 1);
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
static Object* map_get(Map* map, char* key) {

    size_t key_hash = hash(key);
    key_hash = key_hash  % (map->size);
    if (map->data[key_hash].key != NULL) {
        if (strcmp(map->data[key_hash].key, key) == 0) {
            /* Found the item, return the object */
            return map->data[key_hash].value;
        }

        /* Found a collision, iterate over the rest of the table to find the map entry if it exists */
        while (map->data[key_hash].key != NULL) {
            /* Found the object after the collision, return the object */
            if (strcmp(map->data[key_hash].key, key) == 0) {
                /* Found the item, return the object */
                return map->data[key_hash].value;
            }

            key_hash += 1;
            key_hash %= map->size;

        }

    }

    /* The value we were looking for isn't in the table */
    return NULL;
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

Object* eval(Map* env, Object* obj);
static void init_env(Map* env);

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
        return string_new("\"nil");
    }
    switch (obj->type) {
        case NUMBER:
            return string_new("\"number");
        case SYMBOL:
            return string_new("\"symbol");
        case STRING:
            return string_new("\"string");
        case ERROR:
            return string_new("\"error");
        case FUNCTION:
            return string_new("\"function");
        case MACRO:
            return string_new("\"macro");
        case CONS:
            return string_new("\"cons");
        case BOOL:
            return string_new("\"bool");
        default:
            return string_new("\"unknown");
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

/*
 * Start of scanning and parsing functions
 */

static Object* expr(char** str);
static Object* list(char** str);
static Object* atom(char** str);

/**
 * Clears the buffer so it can be re-used. Ideally buffers are
 * allocated on the stack, re-used and cleared for re-use by this
 * function.
 * @param buff - the buffer of characters to clear
 */
static void clear_buff(char buff[]) {
    int i;
    for (i = 0; i < BUFF_SIZE; i++) {
        buff[i] = 0;
    }
}

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
 * Determines the next token in the input stream and stores
 * the generated token in the provided buffer. Buff is allocated
 * on the stack.
 * @param str - the input stream
 * @param buff - the provided buffer
 * @return - how many characters of the input stream were consumed or -1 if an error occurred during scanning
 */
static Object* peek(char** str, char buff[]) {

    Object* res;
    char* temp;
    char consumed;
    char c;
    clear_buff(buff);

    temp = *str;
    consumed = 0;
    c = *temp;
    res = NULL;

    while (c != 0) {

        c = *(temp++);

        if (c == '(') {
            consumed++;
            *buff = '(';
            break;
        }

        if (c == ')') {
            consumed++;
            *buff = ')';
            break;
        }

        if (c == '\'') {
            consumed++;
            *buff = '\'';
            break;
        }

        if (c == '`') {
            consumed++;
            *buff = '`';
            break;
        }

        if (c == ',') {
            consumed++;
            *buff = ',';
            break;
        }

        /* Ignore whitespace, tabs, returns and newlines */
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            consumed++;
            continue;
        }

        if (is_number(c)) {
            while (is_number(c)) {
                consumed++;
                *(buff++) = c;
                c = *(temp++);
            }
            break;
        }

        if (c == '"') {
            consumed++;
            *(buff++) = c;
            c = *(temp++);
            while (c != '"') {

                if (c == '\n') {
                    return error_new("syntax error: found EOL while scanning string");
                }

                if (c == EOF) {
                    return error_new("syntax error: found EOF while scanning string");
                }

                consumed++;
                *(buff++) = c;
                c = *(temp++);
            }
            consumed++;
            break;
        }

        /* Everything after ; is part of a comment until the next newline */
        if (c == ';') {
            while (c != '\n') {
                c = *(temp++);
                consumed++;
            }
            break;
        }

        if (is_symbol(c)) {
            while (is_symbol(c)) {
                consumed++;
                *(buff++) = c;
                c = *(temp++);
            }
            break;
        }

    }

    return number_new(consumed);

}

/**
 * Determines if the string/token in the buffer is an atom
 * @param buff - contains a string/token under consideration
 * @return - true or false
 */
static char is_atom(char buff[]) {
    if (is_number(*buff) || is_string(*buff) || is_symbol(*buff)) {
        return 1;
    }
    return 0;
}

/**
 * Determines if the string/token in the buffer is an expr, i.e.
 * either the beginning of a list or an atom
 * @param buff - contains the string/token under consideration
 * @return - true or false
 */
static char is_expr(char buff[]) {
    if (*buff == '\'' || *buff == '`' || *buff == ',' || *buff == '(' || is_atom(buff)) {
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

/**
 * Like peek() except it advances the input stream after
 * producing each token.
 * @param str - the input stream
 * @param buff - the buffer to contain the token
 */
static Object* next(char** str, char buff[]) {
    Object* obj = peek(str, buff);
    if (is_type(obj, NUMBER)) {
        *str += obj->data.num;
    } else if (is_type(obj, ERROR)) {
        return obj;
    }
    return NULL;
}

static Map* push_local_environment(void) {
    /* Create new environment and push onto environment stack */
    gc->env_stack[++gc->tos] = map_new(INITIAL_ENV_SIZE);
    return gc->env_stack[gc->tos];
}

static void pop_local_environment(void) {
    /* Pop local environment */
    map_free(gc->env_stack[gc->tos]);
    gc->env_stack[gc->tos] = NULL;
    gc->tos--;
}

static Object* function_wrapper(Object* function, Object* args[]) {
    
    Map* local_env;
    Object* temp;
    size_t i;
    Object* res;
    /* Create new environment and push onto environment stack */
    local_env = push_local_environment();
    
    temp = function->data.fn.args;
    
    /* Store function arguments inside local environment */
    i = 0;
    while (car(temp) != NULL) {
        Object* name = car(temp);
        map_put(local_env, name->data.str, args[i]);
        temp = cdr(temp);
        i++;
    }
    
    res = eval(gc->env_stack[gc->tos], function->data.fn.body);
    
    /* Cleanup local environment after function has finished executing */
    pop_local_environment();
    
    return res;
}

static Object* eval_eval_special_form(Map* env, Object* obj) {
    return eval(env, eval(env, car(cdr(obj))));
}

static Object* eval_quote_special_form(Map* env, Object* obj) {
    return car(cdr(obj));
}

static Object* eval_unquote(Map* env, Object* obj) {

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
static Object* eval_quasiquote_special_form(Map* env, Object* obj) {
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

    return eval(gc->env_stack[gc->tos], cons_new(symbol_new("append"), res));
}

static Object* eval_define_special_form(Map* env, Object* obj) {
    Object* name = car(cdr(obj));
    Object* value = car(cdr(cdr(obj)));
    /* the define special form stores values in the root environment */
    map_put(gc->env_stack[0], name->data.str, eval(env, value));
    return NULL;
}

static Object* eval_let_special_form(Map* env, Object* obj) {
    Map* local_env;
    Object* temp;
    Object* let_body;
    Object* res;

    local_env = push_local_environment();

    temp = car(cdr(obj));
    let_body = car(cdr(cdr(obj)));
    while (temp != NULL) {
        Object* name = car(temp);
        Object* value = eval(env, car(cdr(temp)));

        map_put(local_env, name->data.str, value);
        temp = cdr(cdr(temp));
    }

    res = eval(local_env, let_body);

    pop_local_environment();

    return res;
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

static Object* eval_if_special_form(Map* env, Object* obj) {
    Object* cond = car(cdr(obj));
    Object* true_branch = car(cdr(cdr(obj)));
    Object* else_branch = car(cdr(cdr(cdr(obj))));
    
    Object* result = eval(env, cond);
    if (is_truthy(result)) {
        return eval(env, true_branch);
    }
    
    return eval(env, else_branch);
}

static Object* eval_lambda_special_form(Map* env, Object* obj) {
    
    Object* args = car(cdr(obj));
    Object* body = car(cdr(cdr(obj)));
    return user_defined_function_new(args, body);
    
}

static Object* eval_macro_special_form(Map* env, Object* obj) {
    
    Object* args = car(cdr(obj));
    Object* body = car(cdr(cdr(obj)));
    return user_defined_macro_new(args, body);
    
}

static Object* eval_do_special_form(Map* env, Object* obj) {
    
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
static Object* eval_special_form(Map* env, Object* obj) {
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
static Object* eval_function_call(Map* env, Object* obj, char expand_macro) {

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
    char error_buff[255];

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
    while (car(temp) != NULL) {
        
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
        result = function->data.fn.fn(arg_array);
    } else {
        result = function_wrapper(function, arg_array);
    }

    /* If function was a macro then evaluate the result */
    if (expand_macro && function->data.fn.is_macro) {
        result = eval(env, result);
    }

    return result;
}

static Object* apply(Object* args[]) {

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

    return eval_function_call(gc->env_stack[gc->tos], cons_new(function, cons), 0);
}

/**
 * Evaluates a cons cell. In common with many lisps it first checks if it's a special form
 * and if not then assumes it's a function call.
 * @param env - the interpreter environment
 * @param obj - an object representing the list to be evaluated
 * @return - the result of evaluating the list
 */
static Object* eval_list(Map* env, Object* obj) {

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
    char buff[BUFF_SIZE] = {0};
    next(str, buff);
    return cons_new(symbol_new("quote"), cons_new(expr(str), NULL));
}

static Object* unquote(char** str) {
    char buff[BUFF_SIZE] = {0};
    next(str, buff);
    return cons_new(symbol_new("unquote"), cons_new(expr(str), NULL));
}

static Object* quasiquote(char** str) {
    char buff[BUFF_SIZE] = {0};
    next(str, buff);
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
    Object* prev;
    Object* obj = NULL;
    Object* err;

    char buff[BUFF_SIZE] = {0};
    err = next(str, buff);
    if (is_type(err, ERROR)) {
        return err;
    }

    err = peek(str, buff);
    if (is_type(err, ERROR)) {
        return err;
    }

    /* found an empty list which represents NULL/nil */
    if (strcmp(buff, ")") == 0) {
        next(str, buff);
        return NULL;
    }

    temp = cons_new(NULL, NULL);
    obj = temp;
    prev = temp;
    while (is_expr(buff)) {
        setcar(temp, expr(str));
        setcdr(temp, cons_new(NULL, NULL));
        prev = temp;
        temp = temp->data.cons.cdr;

        peek(str, buff);
    }

    /* Remove extra empty cons cell, we don't need to free
     * the extra cons cell because we have garbage collection */
    prev->data.cons.cdr = NULL;

    err = next(str, buff);
    if (is_type(err, ERROR)) {
        return err;
    }
    
    if (strcmp(buff, ")") != 0) {
        return error_new("syntax error: missing expected ')'\n");
    }
    return obj;
}

/**
 * atom : Number | String | Symbol
 */
static Object* atom(char** str) {

    Object* obj = NULL;
    char buff[BUFF_SIZE] = {0};
    peek(str, buff);

    /* is_number or is_number with a minus at the beginning*/
    if (is_number(*buff) || (*buff == '-' && is_number(*(buff+1)))) {
        obj = number_new(str_to_int(buff));
    } else if (is_string(*buff)) {
        obj = string_new(buff);
    } else if (is_symbol(*buff)) {
        obj = symbol_new(buff);
    }

    next(str, buff);

    return obj;
}

/**
 * expr : list | atom
 */
static Object* expr(char** str) {
    char buff[BUFF_SIZE] = {0};
    Object* err = NULL;

    err = peek(str, buff);
    if (is_type(err, ERROR)) {
        return err;
    }

    if (strcmp(buff, "'") == 0) {
       /* found a shorthand quote */
        return quote(str);
    } else if (strcmp(buff, "`") == 0) {
        /* found a quasiquote */
         return quasiquote(str);
     } else if (strcmp(buff, ",") == 0) {
         /* found a unquote */
          return unquote(str);
      } else if (strcmp(buff, "(") == 0) {
       /* found a list */
        return list(str);
    } else {
       /* found an atom */
        return atom(str);
    }
}

Object* parse(char** str) {
    return expr(str);
}

Object* read(char* str) {
    return parse(&str);
}

Object* eval(Map* env, Object* obj) {
    size_t i;
    Object* res = NULL;
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

                for (i = gc->tos + 1; i-- > 0; ) {
                    res = map_get(gc->env_stack[i], obj->data.str);
                    if (res != NULL) {
                        break;
                    }
                }
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
            if (p + BUFF_SIZE > buff_size ) {
                buff = realloc(buff, strlen(buff) + buff_size * sizeof(char));
            }
        }
    }

    return buff;
}

static void exec(Map* env, char* str) {
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

        if (gc->objects_since_last_collection >= (gc->objects_at_last_collection * 1.25)) {
            gc_mark(gc);
            gc_sweep(gc);
        }

    }

}

static void exec_tests(Map* env, char* filename, char* str, size_t* pass_count, size_t* fail_count) {
    Object* first;
    Object* obj;
    size_t test_no;
    Map* local_env;
    printf("=== testing (%s) ===\n", filename);
    
    test_no = 0;

    while (strlen(str) > 0) {
        
        obj = parse(&str);
        
        first = car(obj);
        if (first != NULL && strcmp(first->data.str, "deftest") == 0) {
            Object* test_name = car(cdr(obj));
            local_env = push_local_environment();
            obj = eval(local_env, obj);
            pop_local_environment();
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
static Object* macroexpand1(Object* macro) {
    return eval_function_call(gc->env_stack[gc->tos], macro, 0);
}

/**
 * Repeatedly expand the macro until the result is no longer a macro
 * @param macro - the macro to be expanded
 * @return - the expanded macro
 */
static Object* macroexpand(Object* macro) {
    Object* expanded = macroexpand1(macro);
    if (is_type(expanded, MACRO)) {
        return macroexpand(expanded);
    } else if (is_type(expanded, CONS)) {
        Object* expanded_car = eval(gc->env_stack[gc->tos], car(expanded));
        if (is_type(expanded_car, MACRO)) {
            return macroexpand(expanded);
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

Object* builtin_apply(Object* args[]) {
    return apply(args);
}

Object* builtin_car(Object* args[]) {
    return car(args[0]);
}

Object* builtin_setcar(Object* args[]) {
    setcar(args[0], args[1]);
    return NULL;
}

Object* builtin_cdr(Object* args[]) {
    return cdr(args[0]);
}

Object* builtin_setcdr(Object* args[]) {
    setcdr(args[0], args[1]);
    return NULL;
}

Object* builtin_type(Object* args[]) {
    return type(args[0]);
}

Object* builtin_cons(Object* args[]) {
    return cons_new(args[0], args[1]);
}

Object* builtin_print(Object* args[]) {
    print(args[0]);
    printf("\n");
    return NULL;
}

Object* builtin_import(Object* args[]) {
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
        sprintf(error_buff, "import error: '%s' file not found\n", filename);
        error_buff[254] = 0;
        return error_new(error_buff);
    }
    
    str = read_string(fp);
    exec(gc->env_stack[gc->tos], str);
    free(str);
    return NULL;
}

Object* builtin_list(Object* args[]) {
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

Object* builtin_read(Object* args[]) {
    char* str = args[0]->data.str;
    Object* obj = parse(&str);
    return eval(gc->env_stack[gc->tos], obj);
    
}

Object* builtin_append(Object* args[]) {
    
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

Object* builtin_error(Object* args[]) {
    Object* arg = args[0];
    if (arg != NULL) {
        return error_new(arg->data.str);
    }
    return error_new("error");
}

Object* builtin_copy(Object* args[]) {
    return copy(args[0]);
}

Object* builtin_len(Object* args[]) {
    return number_new(length(args[0]));
}

Object* builtin_find(Object* args[]) {
    int pos;
    pos = find(args[0], args[1]);
    
    if (pos > -1) {
        return number_new(pos);
    }
    return NULL;
}

Object* builtin_last(Object* args[]) {
    return last(args[0]);
}

/* func is macro that just combines 'define' and 'lambda' to
 * create named functions */
Object* builtin_func(Object* args[]) {
    Object* name = args[0];
    Object* fn_args = args[1];
    Object* body = args[2];

    return cons_new(symbol_new("define"), cons_new(name, cons_new(cons_new(symbol_new("lambda"), cons_new(fn_args, cons_new(body, NULL))), NULL)));
}

/* defmacro is itself a builtin macro, similar to the func macro */
Object* builtin_defmacro(Object* args[]) {
    Object* name = args[0];
    Object* fn_args = args[1];
    Object* body = args[2];

    return cons_new(symbol_new("define"), cons_new(name, cons_new(cons_new(symbol_new("macro"), cons_new(fn_args, cons_new(body, NULL))), NULL)));
}

Object* builtin_deftest(Object* args[]) {
    
    Object* test_name = args[0];
    Object* test_body = args[1];
    return cons_new(symbol_new("do"), cons_new(cons_new(symbol_new("func"), cons_new(test_name, cons_new(cons_new(NULL, NULL), cons_new(test_body, NULL)))), cons_new(cons_new(test_name, NULL), NULL)));
    
}

Object* builtin_assert(Object* args[]) {
    
    return cons_new(symbol_new("if"), cons_new(args[0], cons_new(symbol_new("true"), cons_new(symbol_new("false"), NULL))));
}

Object* builtin_macroexpand1(Object* args[]) {
    return macroexpand1(args[0]);
}

Object* builtin_macroexpand(Object* args[]) {
    return macroexpand(args[0]);
}

Object* builtin_mark(Object* args[]) {
    gc_mark(gc);
    return NULL;
}

Object* builtin_sweep(Object* args[]) {
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

Object* builtin_equal(Object* args[]) {
    
    return bool_new(is_equal(args[0], args[1]));
    
}

Object* builtin_greater_than(Object* args[]) {
    
    return bool_new(is_greater_than(args[0], args[1]));
    
}

Object* builtin_less_than(Object* args[]) {
    
    return bool_new(is_less_than(args[0], args[1]));
    
}

Object* builtin_plus(Object* args[]) {

    int temp = 0;
    int i = 0;

    while (args[i] != NULL) {
        Object* arg = args[i];
        temp += arg->data.num;
        i++;
    }

    return number_new(temp);
}

Object* builtin_minus(Object* args[]) {

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

Object* builtin_multiply(Object* args[]) {

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

Object* builtin_divide(Object* args[]) {

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

static void init_env(Map* env) {
    
    map_put(env, "nil", NULL);
    map_put(env, "true", bool_new(1));
    map_put(env, "false", bool_new(0));

    map_put(env, "apply", function_new(builtin_apply));
    map_put(env, "car", function_new(builtin_car));
    map_put(env, "setcar", function_new(builtin_setcar));
    map_put(env, "cdr", function_new(builtin_cdr));
    map_put(env, "setcdr", function_new(builtin_setcdr));
    map_put(env, "type", function_new(builtin_type));
    map_put(env, "cons", function_new(builtin_cons));
    map_put(env, "print", function_new(builtin_print));
    map_put(env, "import", function_new(builtin_import));
    map_put(env, "list", function_new(builtin_list));
    map_put(env, "read", function_new(builtin_read));
    map_put(env, "append", function_new(builtin_append));
    map_put(env, "error", function_new(builtin_error));
    map_put(env, "copy", function_new(builtin_copy));
    map_put(env, "len", function_new(builtin_len));
    map_put(env, "find", function_new(builtin_find));
    map_put(env, "last", function_new(builtin_last));
    
    map_put(env, "func", macro_new(builtin_func));
    map_put(env, "defmacro", macro_new(builtin_defmacro));
    map_put(env, "assert", macro_new(builtin_assert));
    map_put(env, "deftest", macro_new(builtin_deftest));
    map_put(env, "macroexpand", function_new(builtin_macroexpand));
    map_put(env, "macroexpand-1", function_new(builtin_macroexpand1));

    map_put(env, "gc-mark", function_new(builtin_mark));
    map_put(env, "gc-sweep", function_new(builtin_sweep));

    map_put(env, "=", function_new(builtin_equal));
    map_put(env, ">", function_new(builtin_greater_than));
    map_put(env, "<", function_new(builtin_less_than));
    map_put(env, "+", function_new(builtin_plus));
    map_put(env, "-", function_new(builtin_minus));
    map_put(env, "*", function_new(builtin_multiply));
    map_put(env, "/", function_new(builtin_divide));
    
    exec(env, "(import \"lib/iteration.lisp\")");

}

/**
 * Provides a Read-Eval-Print-Loop to the Lisp interpreter.
 */
static void repl(Map* env) {

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
    
    Map* env = map_new(INITIAL_ENV_SIZE);
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
    map_free(env);

    return 0;
}
