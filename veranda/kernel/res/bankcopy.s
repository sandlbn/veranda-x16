; Veranda resident core: BankCopy
;
; r0 source, r1 destination, r2 count, r3L the source's bank, r3H the
; destination's: an address at $A000-$BFFF is in that bank (and goes on
; into the next one at $C000); below $A000 the bank does not matter. Byte
; by byte, the banks switched for every byte (about 30 cycles a byte).

.include "kernel.inc"

.export k_BankCopy, VramBank

.segment "CODE"

k_BankCopy:
	lda ram_bank
	pha
	PushW r0
	PushW r1
	PushW r2
	lda r3L
	pha
	lda r3H
	pha
@loop:	lda r2L
	ora r2H
	beq @done
	lda r3L
	sta ram_bank
	lda (r0)
	ldx r3H
	stx ram_bank
	sta (r1)
	inc r0L                 ; source + 1
	bne @s
	inc r0H
	lda r0H
	cmp #$C0
	bne @s
	lda #$A0
	sta r0H
	inc r3L
@s:	inc r1L                 ; destination + 1
	bne @d
	inc r1H
	lda r1H
	cmp #$C0
	bne @d
	lda #$A0
	sta r1H
	inc r3H
@d:	lda r2L                 ; count - 1
	bne @c
	dec r2H
@c:	dec r2L
	bra @loop
@done:	pla
	sta r3H
	pla
	sta r3L
	PopW r2
	PopW r1
	PopW r0
	pla
	sta ram_bank
	rts

; VramBank (desk accessories, bank S): x pages (1-255) between VERA's data
; port 0 (set by the caller) and bank r3L from r1 ($A000-$BFFF, going on
; into the next bank); a = 0 from VRAM to the bank, else back. r1 and r3L
; are left after the last page (a second call goes on). About 15 cycles
; a byte.
VramBank:
	ldy ram_bank
	phy
	ldy r3L
	sty ram_bank
	ldy #0
	cmp #0
	bne @in
@out:	lda VERA_DATA0
	sta (r1),y
	iny
	bne @out
	jsr @next
	bne @out
	bra @done
@in:	lda (r1),y
	sta VERA_DATA0
	iny
	bne @in
	jsr @next
	bne @in
@done:	pla
	sta ram_bank
	rts
@next:	inc r1H
	lda r1H
	cmp #$C0
	bne @1
	lda #$A0
	sta r1H
	inc r3L
	lda r3L
	sta ram_bank
@1:	dex
	rts
