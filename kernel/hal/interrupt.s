;
; interrupt.s
;
%macro ISR_NOERRCODE 1
	global isr%1
	isr%1:
		cli                         ; Disable interrupts firstly.
		push byte 0                 ; Push a dummy error code.
		push byte %1                ; Push the interrupt number.
		jmp isr_common_stub         ; Go to our common handler code.
%endmacro

; This macro creates a stub for an ISR which passes it's own
; error code.
%macro ISR_ERRCODE 1
	global isr%1
	isr%1:
		cli                         ; Disable interrupts.
		push byte %1                ; Push the interrupt number
		jmp isr_common_stub
%endmacro

;
; This macro creates a stub for an IRQ.
; %1 - IRQ number
; %2 - ISR number remap to 
;
%macro IRQ 2
	global irq%1
	irq%1:
		cli
		push byte 0
		push byte %2
		jmp irq_common_stub
%endmacro

ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_NOERRCODE 17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31
ISR_NOERRCODE 128	; Used by system call

IRQ	0, 32
IRQ 	1, 33
IRQ	2, 34
IRQ	3, 35
IRQ	4, 36
IRQ	5, 37
IRQ	6, 38
IRQ	7, 39
IRQ	8, 40
IRQ	9, 41
IRQ	10, 42
IRQ	11, 43
IRQ	12, 44
IRQ	13, 45
IRQ	14, 46
IRQ	15, 47
 
; In isr.c
extern isr_handler

; This is our common ISR stub. It saves the processor state, sets
; up for kernel mode segments, calls the C-level fault handler,
; and finally restores the stack frame.
isr_common_stub:
	pusha                   ; Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax
	push ds
	push es
	push fs

	mov ax, 0x10  		; load the kernel data segment descriptor
	mov ds, ax
	mov es, ax
	mov fs, ax

	call isr_handler

	pop fs
	pop es
	pop ds
	popa                    ; Pops edi,esi,ebp...
	add esp, 8     		; Cleans up the pushed error code and pushed ISR number
	sti
	iret           		; pops 5 things at once: CS, EIP, EFLAGS, SS, and ESP

	
extern irq_handler

; This is our common IRQ stub. It saves the processor state, sets
; up for kernel mode segments, call the C-level fault handler, and
; finally restores the stack frame.
irq_common_stub:
	pusha
	push ds
	push es
	push fs

	mov ax, 0x10		; Load the kernel data segment
	mov ds, ax
	mov es, ax
	mov fs, ax

	call irq_handler

	pop fs
	pop es
	pop ds
	popa
	add esp, 8		; Stack rewind
	sti
	iret			; pops CS, EIP, EFLAGS, SS and ESP at once