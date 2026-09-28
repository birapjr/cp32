    .section .text.start,"ax"
    .align 4
    .global _start
    .type _start,@function
_start:
    /* a2 carries services; incoming a1 points to argc/argv/envp. */
    mov a5,a2
    mov a6,a1
    addi a1,a1,-32
    s32i a5,a1,16
    l32i a2,a6,0
    addi a3,a6,4
    slli a4,a2,2
    add a4,a4,a3
    addi a4,a4,4
    call0 app_main
    l32i a3,a1,16
    beqz a3,1f
    l32i a3,a3,8
    beqz a3,1f
    callx0 a3
1:  j 1b
    .size _start,.-_start
