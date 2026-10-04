	.assume	adl=1

	.equ	ti.InsertMem, 0x020514
	.equ	ti.asm_prgm_size, 0xD0118C
	.equ	ti.userMem, 0xD1A881

	.section	.text

; void *ram_Reserve(size_t size)
; Inserts size bytes after the running program and libraries, which the OS frees when the program exits.
	.global	_ram_Reserve
	.type	_ram_Reserve, @function
_ram_Reserve:
	pop	bc
	pop	hl
	push	hl
	push	bc
	push	ix
	ld	de, (ti.asm_prgm_size)
	push	hl
	ld	hl, ti.userMem
	add	hl, de
	ex	de, hl
	pop	hl
	push	hl
	push	de
	call	ti.InsertMem
	pop	de
	pop	bc
	ld	hl, (ti.asm_prgm_size)
	add	hl, bc
	ld	(ti.asm_prgm_size), hl
	ex	de, hl
	pop	ix
	ret
