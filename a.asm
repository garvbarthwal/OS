ORG 0 ;This tells NASM where in memory our program will run 

BITS 16 ;We are writing a 16-Bit code 

_start:
    jmp short start
    nop 
    
times 33 db 0 
start:
jmp 0x7c0:step2

step2:
    cli ; clear interrupts 
    mov ax,0x7c0
    mov ds,ax
    mov es,ax
    mov ax,0x00
    mov ss,ax
    sti ;enable interrupts 
    mov si,message ;Address of 'message' move into si register 
    call print ;Calls our print function 
    jmp $ ;Infinite loop,GO TO THIS instruction 

    print:
    mov bx,0; clears bx register to use it as a counter 

.loop:
    lodsb ; Loads the byte at address si into al and then increment si 
    cmp al,0 ; Compare the value of al to 0 
    je .done ;If al is equal to 0,then we are at the end of the string and we are done 
    call print_char ;Otherwise print the character in al 
    jmp .loop ;Loop to the next character 

.done:
    ret ; Return from the print statement 

print_char:
    mov ah,0eh ; Move 0eh int ah,this is the bios service , number for printing the character
    int 0x10 ; Interrupt which handle the screen operations 
    ret ; Return from print_char function 

message: db 'Hello world!',0 ;String to be printed terminated by 0 
times 510-($-$$) db 0 ;Fill the rest sector with 0 upto 510 bytes
dw 0xAA55 ;The boot sector signature