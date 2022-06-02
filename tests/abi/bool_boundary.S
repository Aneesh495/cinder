# This is an independent host ABI adversary, never part of Cinder compilation.
.text
.globl abi_return_false
.type abi_return_false, @function
abi_return_false:
    movabs $0xa5a5a5a5a5a5a500, %rax
    ret
.size abi_return_false, .-abi_return_false
.globl abi_return_true
.type abi_return_true, @function
abi_return_true:
    movabs $0xa5a5a5a5a5a5a501, %rax
    ret
.size abi_return_true, .-abi_return_true
.globl abi_poisoned_arguments
.type abi_poisoned_arguments, @function
abi_poisoned_arguments:
    push %rbx
    movabs $0xa5a5a5a5a5a5a500, %rdi
    call cinder_bool_argument
    mov %eax, %ebx
    movabs $0xa5a5a5a5a5a5a501, %rdi
    call cinder_bool_argument
    add %eax, %ebx
    sub $16, %rsp
    movabs $0xa5a5a5a5a5a5a500, %rax
    mov %rax, 0(%rsp)
    mov %rax, %rdi
    mov %rax, %rsi
    mov %rax, %rdx
    mov %rax, %rcx
    mov %rax, %r8
    mov %rax, %r9
    call cinder_bool_stack
    add $16, %rsp
    add %ebx, %eax
    pop %rbx
    ret
.size abi_poisoned_arguments, .-abi_poisoned_arguments
.section .note.GNU-stack,"",@progbits
