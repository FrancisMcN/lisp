#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFF_SIZE 256
#define MAX_FUNC_ARGS 64

/**
 * Converts a string to an integer
 * @param str
 * @return - the parsed integer
 */
static int str_to_int(char* str) {
    return (int) strtol(str, &str, 10);
}

/* The native object types used by the interpreter */
typedef enum {NUMBER, SYMBOL, CONS, FUNCTION, NIL} Type;

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
        char* sym;
        struct Cons {
            struct Object* car;
            struct Object* cdr;
        } cons;
        struct Object* (*fn)(struct Object** args);
    };
} Object;

static Object* object_new();
static void object_free(Object* obj);
static Object* number_new(int num);
static Object* symbol_new(char* symbol);
static Object* function_new(struct Object* (*fn)(struct Object** args));

static void print(Object* obj);

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
static Map* map_new();
static void map_put(Map* map, char* key, Object* obj);
static Object* map_get(Map* map, char* key);
static void map_resize(Map* map);
static void map_free(Map* map);

/**
 * GC is used to implement a mark and sweep garbage collector
 */
typedef struct GC {
    Object* tail;

    Map* env;
} GC;

static GC* gc_new(Map* env) {
    GC* gc = malloc(sizeof(GC));
    gc->env = env;
    return gc;
}

static void gc_mark(GC* gc) {

    size_t i;
    size_t count = 0;
    size_t total = 0;
    /* Mark all objects stored in the environment map */
    MapEntry* entries = gc->env->data;
    for (i = 0; i < gc->env->size; i++) {
        if (entries[i].key != NULL) {
            entries[i].value->marked = 1;
            count++;
        }
    }

    Object* temp = gc->tail;
    while (temp != NULL) {
        total++;
        temp = temp->prev;
    }

    printf("marked %zu objects out of %zu total objects.\n", count, total);

}

static void gc_sweep(GC* gc) {

    size_t count = 0;
    Object* ptr = gc->tail;

    while (ptr != NULL) {

        /* remove unmarked objects */
        if (!ptr->marked) {

            if (ptr->prev != NULL) {
                /* Remove object from middle or end of linked list */
                ptr->prev->next = ptr->next;
                /* The tail pointer also has to be updated if we free'd the last object in the list */
                if (ptr == gc->tail) {
                    gc->tail = gc->tail->prev;
                }
            } else {
                /* Remove object from start of linked list */
                gc->tail = ptr->next;
            }

            Object* temp = ptr;
            ptr = ptr->prev;
            /* Actually free the object */
            object_free(temp);
            count++;

        } else {
            ptr = ptr->prev;
        }

    }

    printf("freed %zu objects.\n", count);

}

static void gc_free(GC* gc) {
    free(gc);
}

/* Instantiate the global reference to the garbage collector,
 * I hate global variables but will allow this one. */
static GC* gc;

/**
 * Creates a new Object using malloc, don't forget to free!
 * @return - returns a pointer to the heap containing the newly created object
 */
static Object* object_new() {
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

    return obj;
}

/**
 * Allocates a new number object on the heap
 * @param num
 * @return - a reference to the number object
 */
static Object* number_new(int num) {
    Object* obj = object_new();
    obj->type = NUMBER;
    obj->num = num;
    return obj;
}

/**
 * Allocates a new symbol object on the heap
 * @param symbol
 * @return - a reference to the symbol object
 */
static Object* symbol_new(char* symbol) {
    unsigned long len;
    Object* obj = object_new();
    obj->type = SYMBOL;

    len = strlen(symbol);
    obj->sym = (char*)malloc(sizeof(char) * (len + 1));
    strcpy(obj->sym, symbol);
    return obj;
}

/**
 * Allocates a new cons object on the heap
 * @param car
 * @param cdr
 * @return
 */
static Object* cons_new(Object* car, Object* cdr) {
    Object* obj = object_new();
    obj->type = CONS;
    obj->cons.car = car;
    obj->cons.cdr = cdr;
    return obj;
}

static Object* function_new(struct Object* (*fn)(struct Object** args)) {
    Object* obj = object_new();
    obj->type = FUNCTION;
    obj->fn = fn;
    return obj;
}

static void object_free(Object* obj) {

    if (obj == NULL) {
        return;
    }

    switch (obj->type) {
        case NUMBER: {
            break;
        }
        case SYMBOL: {
            free(obj->sym);
            break;
        }
        case CONS: {
            break;
        }
        case FUNCTION: {
            break;
        }
        case NIL: {
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
static Map* map_new() {
    unsigned long initial_size = 8;
    Map* map = (Map*)malloc(sizeof(Map));
    map->used = 0;
    map->size = initial_size;
    map->data = malloc(sizeof(MapEntry) * initial_size);
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

/**
 * Re-sizes a map / hash table, each time doubling the size of the table. The
 * table is doubled in size because resizing is an expensive operation, doubling each
 * time means the resize operation becomes less and less frequent as the table grows.
 * @param map - the map to resize
 */
static void map_resize(Map* map) {
    int i;
    /* Increase map size */
    map->size = map->size * 2;
    /* Allocate empty block of data */
    MapEntry* new_data = malloc(sizeof(MapEntry) * map->size);

    /* Re-hash all the existing data */
    MapEntry* old_data = map->data;
    map->data = new_data;
    size_t used = map->used;
    for (i = 0; i < map->used; i++) {
        if (old_data[i].key != NULL) {
            map_put(map, old_data[i].key, old_data[i].value);
        }
    }
    map->used = used;

    /* Free old block of memory */
    free(old_data);

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
    size_t key_len = strlen(key);

    if (map->used == map->size - 1) {
        map_resize(map);
    }

    size_t key_hash = hash(key) % (map->size - 1);
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
        char* map_key = malloc(sizeof(char) * key_len + 1);
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
    key_hash = key_hash  % (map->size - 1);
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
    if (map != NULL) {
        free(map->data);
        free(map);
    }
}

/* End of hash table implementation */

Object* eval(Map* env, Object* obj);

static Object* car(Object* obj) {
    if (obj != NULL && obj->type == CONS) {
        return obj->cons.car;
    }
    return NULL;
}

static Object* cdr(Object* obj) {
    if (obj != NULL && obj->type == CONS) {
        return obj->cons.cdr;
    }
    return NULL;
}

static void print(Object* obj) {

    if (obj == NULL) {
        printf("nil");
        return;
    }

    switch (obj->type) {
        case NUMBER: {
            printf("%d", obj->num);
            break;
        }
        case SYMBOL: {
            printf("%s", obj->sym);
            break;
        }
        case CONS: {
            putchar('(');
            Object* temp = obj;
            while (temp != NULL) {
                print(car(temp));
                if (cdr(temp) != NULL) {
                    putchar(' ');
                }
                temp = cdr(temp);
            }
            putchar(')');
            break;
        }
        case FUNCTION: {
            printf("%p", obj->fn);
            break;
        }
        case NIL: {
            break;
        }
    }
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
 * @param buff
 */
static void clear_buff(char buff[]) {
    int i;
    for (i = 0; i < BUFF_SIZE; i++) {
        buff[i] = 0;
    }
}

/**
 * Determines whether the provided character c is numeric
 * @param c
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
 * @param c
 * @return true or false
 */
static char is_symbol(char c) {
    if (is_printable(c)) {
        switch (c) {
            case '(':
            case ')':
            case '\'':
                return 0;
            default:
                return 1;
        }
    }
    return 0;
}

/**
 * Determines the next token in the input stream and stores
 * the generated token in the provided buffer. Buff is allocated
 * on the stack.
 * @param str - the input stream
 * @param buff - the provided buffer
 * @return - how many characters of the input stream were consumed
 */
static char peek(char** str, char buff[]) {


    clear_buff(buff);

    char* temp = *str;
    char consumed = 0;
    char c = *temp;

    while (c != 0) {

        consumed++;
        c = *(temp++);

        if (c == '(') {
            *buff = '(';
            break;
        }

        if (c == ')') {
            *buff = ')';
            break;
        }

        if (is_number(c)) {
            while (is_number(c)) {
                consumed++;
                *(buff++) = c;
                c = *(temp++);
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

    return consumed;

}

/**
 * Like peek() except it advances the input stream after
 * producing each token.
 * @param str - the input stream
 * @param buff - the buffer to contain the token
 */
static void next(char** str, char buff[]) {
    char consumed = peek(str, buff);
    *str += consumed;
}

/**
 * Determines if the string/token in the buffer is an atom
 * @param buff - contains a string/token under consideration
 * @return - true or false
 */
static char is_atom(char buff[]) {
    if (is_number(*buff) || is_symbol(*buff)) {
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
    if (*buff == '(' || is_atom(buff)) {
        return 1;
    }
    return 0;
}

/**
 * Tests the type of an object, just a helper method to
 * reduce some duplicated code.
 * @param object
 * @param obj_type
 * @return
 */
static char is_type(Object* object, Type obj_type) {
    if (object != NULL) {
        return (char) object->type == obj_type;
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
        char* sym = first->sym;
        if (strcmp(sym, "quote") == 0 ||
            strcmp(sym, "eval") == 0 ||
            strcmp(sym, "define") == 0 ||
            strcmp(sym, "if") == 0) {
            return 1;
            }
    }
    return 0;
}

static Object* eval_eval_special_form(Map* env, Object* obj) {

    return eval(env, eval(env, car(cdr(obj))));
}

static Object* eval_quote_special_form(Map* env, Object* obj) {
    return car(cdr(obj));
}

static Object* eval_define_special_form(Map* env, Object* obj) {
    Object* name = car(cdr(obj));
    Object* value = car(cdr(cdr(obj)));
    map_put(env, name->sym, eval(env, value));
    return NULL;
}

static Object* eval_if_special_form(Map* env, Object* obj) {
    Object* first = car(obj);
    printf("found if special form\n");
    return NULL;
}

/**
 * Evaluates the special form, uses the environment to store side effects
 * @param env
 * @param obj
 * @return
 */
static Object* eval_special_form(Map* env, Object* obj) {
    Object* first = car(obj);
    if (strcmp(first->sym, "quote") == 0) {
        return eval_quote_special_form(env, obj);
    }
    if (strcmp(first->sym, "eval") == 0) {
        return eval_eval_special_form(env, obj);
    }
    if (strcmp(first->sym, "define") == 0) {
        return eval_define_special_form(env, obj);
    }
    if (strcmp(first->sym, "if") == 0) {
        return eval_if_special_form(env, obj);
    }
    return NULL;
}

/**
 * Evaluates a cons cell representing a function call
 * @param env
 * @param obj
 * @return
 */
static Object* eval_function_call(Map* env, Object* obj) {
    int i;
    Object* function = eval(env, car(obj));
    Object* args[MAX_FUNC_ARGS] = {0};

    Object* temp = obj;
    i = 0;
    while (car(temp) != NULL) {
        temp = cdr(temp);
        args[i++] = eval(env, car(temp));

    }

    return function->fn(args);
}

/**
 * Evaluates a cons cell. In common with many lisps it first checks if it's a special form
 * and if not then assumes it's a function call.
 * @param env
 * @param obj
 * @return
 */
static Object* eval_list(Map* env, Object* obj) {

    if (is_special_form(obj)) {
        return eval_special_form(env, obj);
    }

    return eval_function_call(env, obj);
}

/**
 * Parses a list and produces a cons object.
 * list : '(' expr* ')'
 * @param str - the input stream
 * @return - a cons object
 */
static Object* list(char** str) {

    Object* obj = NULL;
    char buff[BUFF_SIZE] = {0};
    next(str, buff);
    
    peek(str, buff);

    Object* temp = cons_new(NULL, NULL);
    obj = temp;
    Object* prev = temp;
    while (is_expr(buff)) {
        temp->cons.car = expr(str);

        temp->cons.cdr = cons_new(NULL, NULL);
        prev = temp;
        temp = temp->cons.cdr;

        peek(str, buff);
    }

    /* Remove extra empty cons cell, we don't need to free
     * the extra cons cell because we have garbage collection */
    prev->cons.cdr = NULL;

    next(str, buff);
    return obj;
}

/**
 * atom : Integer | Symbol
 */
static Object* atom(char** str) {

    Object* obj = NULL;
    char buff[BUFF_SIZE] = {0};
    peek(str, buff);

    if (is_number(*buff)) {
        obj = number_new(str_to_int(buff));
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
    peek(str, buff);
    if (strcmp(buff, "(") == 0) {
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
    Object* res = NULL;
    if (obj != NULL) {
        switch (obj->type) {
            case SYMBOL: {
                res = map_get(env, obj->sym);
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

/*
 * End of scanning and parsing functions
 */

Object* builtin_car(Object* args[]) {
    return car(args[0]);
}

Object* builtin_cdr(Object* args[]) {
    return cdr(args[0]);
}

Object* builtin_cons(Object* args[]) {
    return cons_new(args[0], args[1]);
}

Object* builtin_mark(Object* args[]) {
    gc_mark(gc);
    return NULL;
}

Object* builtin_sweep(Object* args[]) {
    gc_sweep(gc);
    return NULL;
}

Object* builtin_plus(Object* args[]) {

    int temp = 0;
    int i = 0;

    while (args[i] != NULL) {
        Object* arg = args[i];
        temp += arg->num;
        i++;
    }

    return number_new(temp);
}

static void init_env(Map* env) {

    map_put(env, "car", function_new(builtin_car));
    map_put(env, "cdr", function_new(builtin_cdr));
    map_put(env, "cons", function_new(builtin_cons));

    map_put(env, "gc-mark", function_new(builtin_mark));
    map_put(env, "gc-sweep", function_new(builtin_sweep));

    map_put(env, "+", function_new(builtin_plus));


}

/**
 * Provides a Read-Eval-Print-Loop to the Lisp interpreter.
 */
static void repl() {
    Object* obj;
    Object* res;
    char buff[BUFF_SIZE] = {0};

    Map* env = map_new();
    gc = gc_new(env);
    init_env(env);

    while (strcmp(buff, "(exit)") != 0) {
        printf("> ");
        read_stdin(buff);

        if (buff[0] == '\n') {
            continue;
        }

        obj = read(buff);

        res = eval(env, obj);

        print(res);
        printf("\n");

    }

    gc_free(gc);
    map_free(env);

}

int main(void) {

    repl();

    return 0;
}
