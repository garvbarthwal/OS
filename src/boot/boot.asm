; boot_pm.asm
; NASM boot sector: real mode -> protected mode, with simple GDT
; Assemble: nasm -f bin boot_pm.asm -o boot_pm.bin

org 0x7c00
BITS 16

jmp start
nop                 ; padding for BIOS (optional)

; ----- real mode start -----
start:
    cli
    ; Setup real-mode segment registers and stack
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00      ; temporary stack in real mode
    sti

    ; Load GDT and enable protected mode
    lgdt [gdt_descriptor]

    ; Set PE bit in CR0
    mov eax, cr0
    or  eax, 1
    mov cr0, eax

    ; Far jump to flush prefetch and load new CS (selector 0x08)
    jmp 0x08:protected_entry

; ----- padding to keep code tidy if needed -----
; (no other real-mode code expected below)

; ----- GDT -----
gdt_start:
gdt_null:               ; descriptor 0 (null)
    dq 0x0000000000000000

; Code segment descriptor (index 1 -> selector 0x08)
; base = 0x00000000, limit = 0xFFFFF (with granularity), access = 0x9A, gran = 0xCF
gdt_code:
    dw 0xFFFF            ; limit low
    dw 0x0000            ; base low
    db 0x00              ; base middle
    db 0x9A              ; access
    db 0xCF              ; granularity (limit high nibble + flags)
    db 0x00              ; base high

; Data segment descriptor (index 2 -> selector 0x10)
gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92
    db 0xCF
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ----- protected-mode entry point -----
[BITS 32]
protected_entry:
    ; Load data segment selectors (16-bit selector values)
    mov ax, 0x10          ; data selector (index 2 << 3 = 0x10)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Setup stack (32-bit)
    mov esp, 0x90000      ; example stack (below 1MB). Adjust as needed.

    ; Now in protected mode. For demo, we'll jump to an infinite loop.
    ; Replace this with kernel load / further init as required.
.hang:
    cli
    hlt
    jmp .hang

; ----- fill to 512 bytes and add boot signature -----
times 510 - ($ - $$) db 0
dw 0xAA55
