#include "cinder.h"

bool cinder_ir_floating(const CinderType *type) {
    return type != NULL && (type->kind == TYPE_FLOAT || type->kind == TYPE_DOUBLE);
}

CinderType *cinder_ir_value_type(const CinderIRFunction *function, CinderValueId value) {
    for (size_t b = 0U; b < function->blocks.len; ++b)
        for (size_t i = 0U; i < function->blocks.data[b].instructions.len; ++i)
            if (function->blocks.data[b].instructions.data[i].dst == value) return function->blocks.data[b].instructions.data[i].type;
    return NULL;
}

const char *cinder_ir_op_name(CinderIROp op) {
    static const char *names[] = {"nop","const","fconst","global.load","global.store","local.load","local.store","arg","farg","va_arg","copy","add","sub","mul","fadd","fsub","fmul","fdiv","fneg","fcmp.eq","fcmp.ne","fcmp.lt","fcmp.le","fcmp.gt","fcmp.ge","div.s","div.u","mod.s","mod.u","neg","not","and","or","xor","shl","shr.s","shr.u","cmp.eq","cmp.ne","cmp.lt.s","cmp.le.s","cmp.gt.s","cmp.ge.s","cmp.lt.u","cmp.le.u","cmp.gt.u","cmp.ge.u","call","phi","convert","local.address","global.address","memory.load","memory.store","pointer.offset","pointer.diff","pointer.member","local.begin","local.end","local.reset","function.address","object.copy","object.init","local.init","memory.init","zero.init","local.freeze","aggregate.arg","aggregate.return","va.start","va.copy","va.end","bit.load","bit.store","bit.init","bit.convert","undef"};
    return op < CINDER_ARRAY_LEN(names) ? names[op] : "unknown";
}
