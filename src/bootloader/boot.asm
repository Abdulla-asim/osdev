;
; Bootloader
;
org 0x7c00
bits 16

%define ENDL 0x0D, 0x0A

;
; FAT12 Header
;
jmp short start				; 3 
nop							; Bytes

;
; Boot D
;
bpb_oem_id: 				db "MSWIN4.1"		; 8 Bytes
bpb_bytes_per_sector:		dw 0x0200			; 2 Bytes (Little Endian, i.e: 0x0020 after assembly)->(512)
bpb_sectors_per_cluster:	db 0x1				; 1 Bytes, Cluster: Smalles Logical Storage Unit Assigned to Files
bpb_reserved_sectors:		dw 0x1				; 2 Bytes, Boot Record Sectors Included
bpb_num_of_FATs:			db 0x2				; 1 Bytes, Default value: 2 (1 Main, 1 Backup)
bpb_num_of_root_entries:	dw 0xe0				; 2 Bytes, 224 (OE0) x 32(dir entry size) = 7KiB (Total avaialble space for root dir) (14 sectors in this case)
bpb_total_sectors:			dw 2880				; 2 Bytes, 1440 (KiB) * 1024 (1 Ki) / 512 (sector size) = 2880 (0x0B40)
bpb_media_descriptor_type:	db 0xF0				; 1 Bytes, F0 = 3.5" Floppy Disk
bpb_sectors_per_fat:		dw 9
bpb_sectors_per_track: 		dw 18
bpb_heads:					dw 2
bpb_hidden_sectors:			dd 0
bpb_large_sector_count:		dd 0
;
; Extended Boot Record
;
ebpb_drive_number:			db 0				; 0x00 Floppy, 0x80 hdd. Useless
ebpb_reserved_byte:			db 0				; Reserved Byte
ebpb_signature:				db 0x29				; 0x28 or 0x29
ebpb_volume_id:				db 0x12, 0x34, 0x56, 0x78	; Serial Number for identification only
ebpb_volume_label:			db 'AbdullaAsim' 	; 11 Bytes padded with spaces
ebpb_system_id:				db 'FAT12  '		; 8 Bytes padded with spaces

;
; Code From Here On...
;

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
	; mov si, msg_dbg
	; call puts
	; Setup the data segments
	mov ax, 0		; Cant write to segment registers directly
	mov ds, ax
	mov es, ax

	; Setup Stack
	mov ss, ax
	mov sp, 0x7c00	; SS:SP = 0000:7C00 → physical address = 0x7C00 (Stack Grows downward from here)


	; Read data from floppy disk
	; BIOS should set DL to drive number
	mov [ebpb_drive_number], dl

	mov ax, 34						; LBA
	mov cl, 1						; 1 Sector to read
	mov bx, 0x7E00					; Data Should be after the bootlaoder (0x7C00 - 0x7DFF)
	call disk_read

	
	mov si, hello_msg
	call puts	

	mov si, 0x7E00
	call puts

	cli								; Stop the Interrupts so CPU cannot get out of halt
	hlt

;
; ERROR HANDLERS
;

floppy_error:
	mov si, msg_read_failed
	call puts
	jmp wait_key_and_reboot

wait_key_and_reboot:
	mov ah, 0
	int 16h							; Wait for Key Press (Int 16 with function 0)

	jmp 0xFFFF:0					; Jump to the beginning of the BIOS (Reset Vector Address), Should reboot

.halt:
	cli								; Disable Interrupts, (so we cant ever get out of 'HALT' state)
	hlt

;
; DISK ROUTINES
;

;
; Convert the LBA (Logical Block Address) Address to CHS (Cyblinder, Head, Sector)
;
; Formulas:
; 	Sector = (LBA / SectorsPerTrack) + 1
; 	Cylinder = (LBA / SectorsPerTrack) / Heads
; 	Head = (LBA / SectorsPerTrack) % Heads
;
; Parameters:
; 	- ax: LBA Address
; Returns:
;	- cx [0-5]: Sector Number
;	- cx [6-15]: Cylinder
; 	- dh: head number
;
; CX Register Implementation Details:
;
; |F|E|D|C|B|A|9|8|7|6|5-0|  CX
;  | | | | | | | | | |	`-----	sector number
;  | | | | | | | | `---------  high-order 2 bits of track/cylinder
;  `------------------------  low-order 8 bits of track/cyl number
;
; Note: Always remember that sector is 1-based, and not 0-based ... this detail causes many problems.
;
lba_to_chs:
	push ax
	push dx

	xor dx, dx						; dx = 0 (since 32 bit divisor divides dx:ax)
	div word [bpb_sectors_per_track]	; ax = (LBA / SectorsPerTrack)
									; dx = (LBA % SectorsPerTrack)
	inc dx							; dx = (LBA / SectorsPerTrack) + 1 = sector
	mov cx, dx						; cx = dx => Sector
	
	xor dx, dx
	div word [bpb_heads]			; ax = (LBA / SectorsPerTrack) / Heads = "Cylinder"
									; dx = (LBA / SectorsPerTrack) % Heads = "Head"
	mov dh, dl						; dh = Head

	mov ch, al						; ch = al (Lower 8 bits of Cylinder)
	shl ah, 6						
	or cl, ah						; Put upper 2 bits of Cylinder in cl

	pop ax							; Didn't do pop dx because we want to only restore dl (since dh is output)
	mov dl, al						; .. and we can't push 8 bit registers so yea that's why didn't do 'push dl'...
	pop ax							; restore ax

	ret

;
; Read from a Disk
;
; Parameters:
;	- ax: LBA address
;	- cl: Number of sectors to read (upto 128)
;	- dl: Drive Number
;	- es:bx: Memory Address of where to store/read data (Pointer to Buffer)
;
disk_read:
	
	push ax							; Save Registers we will modify
	push bx
	push cx
	push dx
	push di


	push cx							; Save cx, since lba_to_chs modifies cx. (cl = Number of Sectors to read)
	call lba_to_chs					; Conversion to CHS
	pop ax							; Now, al = Number of sectors to read
;
;	- BIOS disk reads should be retried at least three times and the
;	  controller should be reset upon error detection (Real-World Floppy Disks pretty unreliable)
;
	mov ah, 0x2
	mov di, 3						; Retry Count

.retry:
	pusha							; idk what regs will the bios interrupt overwrite so ill just push 'em all
	stc								; Set carry flag before interrupt, Some BIOS's don's set it
	int 13h							; Carry Flag Cleared (0) = "SUCCESS"
	jnc	.done						; Done if "SUCCESS"

	; Failed Disk Read					
	popa
	call disk_reset

	dec di							; Decrement Retry Counter
	test di, di						; ...until 0
	jnz .retry

.fail:
	; All retry attempts exhausted
	jmp floppy_error				; Display Error Message and stop Boot

.done:
	popa

	pop di							; Restore Modified Registers
	pop dx
	pop cx
	pop bx
	pop ax				

	ret

;
; Disk Controller Reset
; Parameters:
;	- dl: Drive Number
;			
disk_reset:
	pusha
	mov ah, 0
	stc
	int 13h							; Disk Reset Interrupt
	jc floppy_error					; If operation fails... (Carry Flag 1 = "FAIL")
	popa

	ret


hello_msg:							db "Assalam-o-Alaikum!", ENDL, 0
msg_read_failed:					db "Disk read failed!", ENDL, 0
msg_dbg:							db "Debug Message...", ENDL, 0

times 510-($-$$) db 0
dw 0AA55h
