
# OS: A 32-bit Protected Mode Kernel

This repository contains the source code for a minimalistic 32-bit operating system for the x86 architecture, written from scratch. It demonstrates the entire boot process, from the initial 16-bit real mode execution to a fully operational 32-bit C kernel.

## Core Concepts

This project implements several fundamental concepts of operating system design:

-   **Real Mode vs. Protected Mode:** The kernel manages the crucial transition from the CPU's initial 16-bit Real Mode to the 32-bit Protected Mode, unlocking 4GB of memory and features like memory protection.
    
-   **Master Boot Record (MBR):** The project starts with a 512-byte bootloader located in the MBR, which is identified by the BIOS using the `0xAA55` signature.
    
-   **Bootloader:** The bootloader (`boot.asm`) is responsible for preparing the system for the 32-bit environment and loading the main C kernel from the disk.
    
-   **Global Descriptor Table (GDT):** A GDT is defined and loaded by the bootloader to establish a memory model with distinct code and data segments, a requirement for entering Protected Mode.
    
-   **Interrupts:** The kernel handles both hardware and software interrupts.
    
    -   **Programmable Interrupt Controller (PIC):** The PIC is remapped to prevent conflicts between hardware interrupts and CPU exceptions.
        
    -   **Interrupt Descriptor Table (IDT):** A full IDT is set up in the kernel to route interrupts and exceptions to their respective C handlers.
        
    -   **Enabling/Disabling Interrupts:** The kernel uses `sti` and `cli` to manage critical sections where interrupts must be temporarily disabled.
        
-   **ATA Driver:** A simple, low-level ATA driver is implemented in the bootloader to read the kernel from the hard disk using direct I/O port communication.
    
-   **Kernel Memory Allocation & Heap:** The kernel includes a custom dynamic memory allocator. A block-based heap is implemented to allow the kernel to manage its own memory without relying on an underlying OS.
    

----------

## The Boot Process: A Detailed Walkthrough

The boot sequence is a carefully orchestrated multi-stage process.

### Stage 1: The BIOS and Real Mode

1.  **Power On:** The CPU starts in 16-bit **Real Mode** and begins executing the BIOS firmware.
    
2.  **POST:** The BIOS performs a Power-On Self-Test to check hardware.
    
3.  **Finding a Bootable Device:** The BIOS searches for a device with a valid MBR, identified by the `0xAA55` magic number at the end of its first sector.
    
4.  **Loading the Bootloader:** The BIOS copies the 512-byte MBR from the disk into physical memory at address `0x7c00`.
    
5.  **The First Jump:** The BIOS jumps to `0x7c00`, handing over complete control to our bootloader code.
    

### Stage 2: The Bootloader's Protected Mode Transition

The `boot.asm` code is now running in Real Mode. Its primary goal is to switch to 32-bit Protected Mode.

1.  **Disable Interrupts:** The first action is `cli` to prevent interrupts from interfering with the mode switch.
    
2.  **Load the GDT:**
    
    -   A GDT with null, code, and data segment descriptors is defined.
        
    -   The `lgdt` instruction loads the address and size of this table into the CPU's GDTR register.
        
3.  **Enable Protected Mode:**
    
    -   The protected mode enable bit in the `CR0` control register is set.
        
    -   `mov eax, cr0`
        
    -   `or eax, 0x1`
        
    -   `mov cr0, eax`
        
    -   The CPU is now officially in 32-bit Protected Mode.
        
4.  **Flush the CPU Pipeline:** A far jump is performed to clear the CPU's instruction pipeline of any 16-bit instructions and to load the new code segment selector from the GDT.
    
    > `jmp CODE_SEG:load32`
    

### Stage 3: The Kernel Loader (32-bit)

The bootloader is now executing 32-bit code.

1.  **Set Up Segment Registers:** The data segment selector is loaded into `DS`, `ES`, `SS`, etc.
    
2.  **Create a Stack:** The stack pointer (`ESP`) is set to a safe, high memory address (`0x00200000`).
    
3.  **Load the Kernel:** The `ata_lba_read` function is called to read the kernel from the disk (100 sectors starting at LBA 1) and place it at memory address `0x0100000`.
    
4.  **Final Jump to the Kernel:** The bootloader's job is complete. It jumps to the kernel's entry point.
    
    > `jmp CODE_SEG:0x0100000`
    

### Stage 4: The Kernel Takes Control

The CPU is now executing the `_start` label in `kernel.asm`.

1.  **Kernel Initialization:** The kernel sets up its own segment registers and stack.
    
2.  **PIC Remapping:** The PIC is reprogrammed to prevent interrupt conflicts.
    
3.  **Call `kernel_main`:** The assembly entry point calls the main C function, `kernel_main`.
    
4.  **High-Level C Setup:** Inside `kernel_main`, the high-level kernel services are initialized in order:
    
    -   The VGA terminal is initialized for screen output.
        
    -   The kernel heap is set up for dynamic memory allocation.
        
    -   The Interrupt Descriptor Table (IDT) is initialized to handle exceptions and hardware interrupts.
        
    -   Finally, interrupts are re-enabled globally with `sti`.
        

**The operating system is now fully booted and running.**

Contributors
1. Garv Barthwal