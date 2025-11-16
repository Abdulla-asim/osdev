; Kernel

org 0x7c00
bits 16

%define ENDL 0x0D, 0x0A

start:
	jmp main
;
; Function to print a string
; Params:
;	ds:si points to string
;
puts:
	; Save registers we will use
	push si
	push ax

.loop:
	lodsb			; Loads next character (from si) into al
	or al, al		; Is the next character is null? (set zero flag)
	jz .done

	mov ah, 0x0E	; TTY Mode
	mov bh, 0		; Page Number = 0
	int 0x10		; Video Interrupt


	jmp .loop

.done:
	pop ax
	pop si
	ret

main:
	; Setup the data segments
	mov ax, 0		; Cant write to segment registers directly
	mov ds, ax
	mov es, ax

	; Setup Stack
	mov ss, ax
	mov sp, 0x7c00	; Stack Grows downward fromhere		

	mov si, hello_msg
	call puts	

	hlt

.halt:
	jmp .halt

hello_msg: db "Assalam-o-Alaikum!", ENDL, 0

times 510-($-$$) db 0
dw 0AA55h
