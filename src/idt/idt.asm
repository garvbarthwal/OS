section.asm
global idt_load:
idt_load:
    push ebp
    mov epb,esp

    mov ebx,[ebp+8]
    lidt [ebx]
    pop ebp
    ret