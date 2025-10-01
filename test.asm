[BITS 16]
[ORG 0x7C00]          ; BIOS loads boot sector here

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00     ; safe stack
    sti

    mov si, message    ; SI points to message
    call print

hang:
    jmp hang           ; infinite loop

; Print routine
print:
    lodsb              ; load byte at SI into AL
    cmp al, 0
    je .done
    call print_char
    jmp print
.done:
    ret

print_char:
    mov ah, 0x0E
    int 0x10
    ret

message db 'Hello, world!', 0

times 510-($-$$) db 0
dw 0xAA55
