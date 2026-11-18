; Veranda shell: the desktop's picture to the screens (wall.c, DECISIONS
; V78), in the resident part: it maps the picture's banks, which an
; overlay (at $A000 itself) cannot.
;
; wall_blit copies _wbRows rows of _wbN bytes from RAM bank _wbBank at
; _wbPtr (a row every _wbStep bytes; a bank holds 102 rows, 8,160 bytes:
; _wbLeft rows of it are left, then the next bank, _wbPer rows a bank) to the screens _wbSel selects (dispBufferOn:
; bit 7 the foreground at $00000, bit 6 the back screen at $09600; a row
; 80 bytes), from screen offset _wbV (y * 80 + x). VERA's data port 0
; writes the foreground, port 1 the back screen.
.include "veranda.inc"
.include "x16hw.inc"

.export _wall_blit, _wbBank, _wbPtr, _wbN, _wbRows, _wbV, _wbSel, _wbStep, _wbLeft, _wbPer

ROWS = 8160                     ; a bank's rows: 102 of 80 bytes

.segment "ZEROPAGE"
_wbPtr:	.res 2

.segment "BSS"
_wbBank: .res 1
_wbN:	.res 1
_wbRows: .res 2
_wbV:	.res 2
_wbSel:	.res 1
_wbStep: .res 1
_wbLeft: .res 1
_wbPer:	.res 1

.segment "CODE"

_wall_blit:
	lda $00
	pha
@row:	lda _wbBank
	sta $00
	stz VERA_CTRL
	lda _wbV                ; port 0: the foreground
	sta VERA_ADDR_L
	lda _wbV+1
	sta VERA_ADDR_M
	lda #VERA_INC1
	sta VERA_ADDR_H
	lda #1                  ; port 1: the back screen
	sta VERA_CTRL
	lda _wbV
	sta VERA_ADDR_L
	lda _wbV+1
	clc
	adc #$96
	sta VERA_ADDR_M
	lda #VERA_INC1
	adc #0
	sta VERA_ADDR_H
	stz VERA_CTRL
	ldy #0
	ldx _wbN
	bit _wbSel
	bpl @back
	bvc @fore
@both:	lda (_wbPtr),y
	sta VERA_DATA0
	sta VERA_DATA1
	iny
	dex
	bne @both
	bra @next
@fore:	lda (_wbPtr),y
	sta VERA_DATA0
	iny
	dex
	bne @fore
	bra @next
@back:	lda (_wbPtr),y
	sta VERA_DATA1
	iny
	dex
	bne @back
@next:	lda _wbV                ; the next screen row
	clc
	adc #80
	sta _wbV
	bcc @1
	inc _wbV+1
@1:	lda _wbPtr              ; the next row of the picture
	clc
	adc _wbStep
	sta _wbPtr
	bcc @2
	inc _wbPtr+1
@2:	dec _wbLeft             ; past the bank's rows: the next bank
	bne @3
	lda _wbPer
	sta _wbLeft
	lda _wbPtr
	sec
	sbc #<ROWS
	sta _wbPtr
	lda _wbPtr+1
	sbc #>ROWS
	sta _wbPtr+1
	inc _wbBank
@3:	lda _wbRows
	bne @5
	dec _wbRows+1
@5:	dec _wbRows
	lda _wbRows
	ora _wbRows+1
	beq @end
	jmp @row
@end:	pla
	sta $00
	rts
