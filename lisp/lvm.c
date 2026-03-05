/* Lisp VM */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STACK_SIZE           1024 * 256 /* ~2MB */
#define MAX_FRAMES           512
#define BLOCK_SIZE           64

#define READ_INSTR()         (*ip++)
#define PEEK_INSTR()         (*ip)
#define OP_CODE(i)           ((i) >> 24)

/* ============================================================
 *  BYTECODE  —  32-bit register-based instructions
 *
 *  Encoding:
 *    [31..24]  opcode   (8 bits)
 *    [23..16]  A        (8 bits)  — destination register
 *    [15..8]   B        (8 bits)  — source / operand 1
 *    [7..0]    C        (8 bits)  — source / operand 2
 *
 *  For immediate-wide forms (iBx, iAx):
 *    [23..8]   Bx       (16 bits, unsigned)
 *    [23..0]   Ax       (24 bits, signed — for jumps)
 *
 *  Register-based VMs map cleanly to hardware registers in JIT:
 *  each "virtual register" becomes a physical register or stack slot.
 * ============================================================ */

#define MAKE_ABC(op,a,b,c)   (((uint32_t)(op)<<24)|((a)<<16)|((b)<<8)|(c))
#define MAKE_ABx(op,a,bx)    (((uint32_t)(op)<<24)|((a)<<16)|((bx)&0xFFFF))
#define MAKE_Ax(op,ax)       (((uint32_t)(op)<<24)|((ax)&0x00FFFFFF))

#define OP_A(i)              (((i) >> 16) & 0xFF)
#define OP_B(i)              (((i) >>  8) & 0xFF)
#define OP_C(i)              (((i)      ) & 0xFF)

typedef unsigned int Instr;

typedef enum {
    OP_MOV,
    OP_LOADC,
    OP_ADD,
    OP_STOP
} Opcode;

typedef enum {
    T_NIL,
    T_BOOL,
    T_INT,       /* Unboxed 64-bit int */
    T_FLOAT,     /* Unboxed 64-bit double */
    T_PTR        /* Heap allocated objects */
} Type;

/* Beginning of Value type and associated functions */
typedef struct {
    Type type;
    union {
        long i;
        double f;
        char b;
        void* p;
    } val;
} Value;

static Value val_nil(void) {
    Value val;
    val.type = T_NIL;
    val.val.p = NULL;
    return val;
}

static Value val_bool(char b) {
    Value val;
    val.type = T_BOOL;
    val.val.b = b;
    return val;
}

static Value val_int(long i) {
    Value val;
    val.type = T_INT;
    val.val.i = i;
    return val;
}

static Value val_float(double f) {
    Value val;
    val.type = T_FLOAT;
    val.val.f = f;
    return val;
}

static Value val_ptr(void* p) {
    Value val;
    val.type = T_PTR;
    val.val.p = p;
    return val;
}

/* End of Value type and associated functions */

/* Beginning of Block type and associated functions */
typedef struct {
    Instr* code;
    long len;
    long cap;
    unsigned char reg_count;
} Block;

Block* block_new(unsigned char reg_count) {
    Block* block = malloc(sizeof(Block));
    block->len = 0;
    block->cap = BLOCK_SIZE;
    block->code = malloc(sizeof(Instr) * block->cap);
    block->reg_count = reg_count;
    return block;
}

void block_emit(Block *b, Instr i) {
    if (b->len == b->cap) {
        b->cap *= 2;
        b->code  = realloc(b->code,  b->cap * sizeof(Instr));
    }
    b->code [b->len] = i;
    b->len++;
}

void block_free(Block* block) {
    free(block);
}

/* End of Block type and associated functions */

/* Beginning of Frame type and associated functions */
typedef struct {
    Instr* ip;
    Value* regs;
} Frame;

/* End of Frame type and associated functions */

/* Beginning of VM type and associated functions */
typedef struct {
    Frame frames[MAX_FRAMES];
    int frame_count;
    Value stack[STACK_SIZE];
    Value* stack_top;
} VM;

VM* vm_new(void) {
    VM* vm = malloc(sizeof(VM));
    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    return vm;
}

Value vm_exec(VM* vm, Block* block) {
    Frame* frame;
    Value* regs;
    int i;
    Instr instr;
    Value result;
    
    /* Setup frame first */
    frame = &vm->frames[vm->frame_count++];
    frame->ip = block->code;
    frame->regs = vm->stack_top;
    
    /* Initialise registers */
    regs = frame->regs;
    vm->stack_top += block->reg_count;
    for (i = 0; i < block->reg_count; i++) {
        regs[i] = val_nil();
    }
    
    Instr* ip = frame->ip;
    result = val_nil();
    
    instr = PEEK_INSTR();
    while (OP_CODE(instr) != OP_STOP) {
        instr = READ_INSTR();
        switch (OP_CODE(instr)) {
            case OP_MOV:
                printf("OP_MOV\n");
                break;
            case OP_LOADC:
                printf("OP_LOADC\n");
                break;
            case OP_ADD:
                printf("OP_ADD\n");
                break;
            case OP_STOP:
                printf("OP_STOP\n");
                break;
        }
    }
    
    return val_int(0);
}

void vm_free(VM* vm) {
    free(vm);
}

/* End of VM type and associated functions */

int main1(int argc, char *argv[]) {
    
    VM* vm = vm_new();
    Block* block = block_new(8);
    block_emit(block, MAKE_ABx(OP_MOV, 10, 5));
    block_emit(block, MAKE_ABx(OP_MOV, 20, 3));
    block_emit(block, MAKE_ABC(OP_STOP, 0, 0, 0));
    vm_exec(vm, block);
    vm_free(vm);
    
    return 0;

}
