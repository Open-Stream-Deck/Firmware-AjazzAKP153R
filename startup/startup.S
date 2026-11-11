.syntax unified
.arch armv7-m
.thumb

.section .vectors, "a", %progbits
.balign 512
.global Vector_Table
Vector_Table:
    .word __stack_top
    .word Reset_Handler
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler
    .word 0, 0, 0, 0
    .word Default_Handler
    .word Default_Handler
    .word 0
    .word Default_Handler
    .word Default_Handler
    .rept 64
    .word Default_Handler
    .endr

.section .text.Reset_Handler, "ax", %progbits
.global Reset_Handler
.type Reset_Handler, %function
.thumb_func
Reset_Handler:
    cpsid i
    ldr r0, =0x40005000
    ldr r1, =0x5555
    strh r1, [r0, #6]
    ldr r1, =0xaaaa
    strh r1, [r0, #6]
    ldr r0, =__stack_top
    msr msp, r0

    ldr r0, =0xe000ed08
    ldr r1, =Vector_Table
    str r1, [r0]
    dsb
    isb
    ldr r0, =__data_load
    ldr r1, =__data_start
    ldr r2, =__data_end
1:
    cmp r1, r2
    bcs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b
2:
    ldr r1, =__bss_start
    ldr r2, =__bss_end
    movs r3, #0
3:
    cmp r1, r2
    bcs 4f
    str r3, [r1], #4
    b 3b
4:
    bl main
    b Default_Handler
.size Reset_Handler, . - Reset_Handler

.section .text.Default_Handler, "ax", %progbits
.global Default_Handler
.type Default_Handler, %function
.thumb_func
Default_Handler:
    ldr r0, =platform_probe_active
    ldrb r1, [r0]
    cbz r1, 6f
    tst lr, #4
    ite eq
    mrseq r2, msp
    mrsne r2, psp
    ldr r3, [r2, #24]
    ldrh r1, [r3]
    and r1, r1, #0xf800
    cmp r1, #0xe800
    ite hs
    addhs r3, r3, #4
    addlo r3, r3, #2
    str r3, [r2, #24]
    ldr r0, =platform_probe_failed
    movs r1, #1
    strb r1, [r0]
    ldr r0, =0xe000ed28
    ldr r1, [r0]
    str r1, [r0]
    ldr r0, =0xe000ed2c
    ldr r1, [r0]
    str r1, [r0]
    bx lr
6:
    cpsid i
5:
    b 5b
.size Default_Handler, . - Default_Handler
