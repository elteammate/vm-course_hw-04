#ifndef VM_COURSE_02_VM_H
#define VM_COURSE_02_VM_H

#include <stdint.h>

#include "runtime_common.h"

typedef uint8_t u8;
typedef uint32_t u32;

struct ReturnStackEntry {
    u32 ip;
    u32 fp;
    u32 lp;
    u32 bp;
};


struct VM_c_repr {
    u32 ip; // instruction pointer
    aint *sp; // stack pointer
    u32 rsp; // return stack pointer
    aint *fp; // frame pointer (arguments)
    aint *lp; // local variables pointer (locals)
    aint *bp; // base pointer (temporaries)
    void *cc; // current closure register
    aint *stack_top;
    u32 rstack_size;
    u32 globals_size;
    u8 *code;
    aint *stack;
    struct ReturnStackEntry *rstack;
    u8 *consts;
    u8 rest[];
};

extern struct VM_c_repr vm;

#endif //VM_COURSE_02_VM_H