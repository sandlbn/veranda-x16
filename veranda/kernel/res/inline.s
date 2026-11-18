; Veranda resident core: calls with inline parameters
;
; An i_ call's parameters follow its jsr. These routines take the return
; address (the jsr's last byte) into returnAddress, read the parameters with
; the caller's bank still mapped, make the call and continue after the
; parameters. They live in the resident core because the caller may be in
; any bank. kRowBuf: a row buffer every bank can see (BitmapUp).

.include "kernel.inc"

.export k_i_Rectangle, k_i_FrameRectangle, k_i_RecoverRectangle, k_i_ImprintRectangle
.export k_i_GraphicsString, k_i_BitmapUp, k_i_PutString, k_i_FillRam, k_i_MoveData
.export k_DoInlineReturn, kRowBuf

.segment "BSS"

kRowBuf:	.res 80

.segment "CODE"

; at the very start of an i_ routine: returnAddress = the caller's
; return address (its jsr's last byte); y = 1
.macro TAKE
	pla
	sta returnAddress
	pla
	sta returnAddress+1
	ldy #1
.endmacro

; .byte top, bottom / .word left, right -> r2L, r2H, r3, r4; y = 7
Rect6:	lda (returnAddress),y
	sta r2L
	iny
	lda (returnAddress),y
	sta r2H
	iny
	lda (returnAddress),y
	sta r3L
	iny
	lda (returnAddress),y
	sta r3H
	iny
	lda (returnAddress),y
	sta r4L
	iny
	lda (returnAddress),y
	sta r4H
	iny
	rts

k_i_Rectangle:
	TAKE
	jsr Rect6
	jsr Rectangle
	lda #7
	jmp k_DoInlineReturn

k_i_RecoverRectangle:
	TAKE
	jsr Rect6
	jsr RecoverRectangle
	lda #7
	jmp k_DoInlineReturn

k_i_ImprintRectangle:
	TAKE
	jsr Rect6
	jsr ImprintRectangle
	lda #7
	jmp k_DoInlineReturn

k_i_FrameRectangle:             ; ... .byte pattern
	TAKE
	jsr Rect6
	lda (returnAddress),y
	jsr FrameRectangle
	lda #8
	jmp k_DoInlineReturn

k_i_BitmapUp:                   ; .word data / .byte x, y, width, height
	TAKE
	lda (returnAddress),y
	sta r0L
	iny
	lda (returnAddress),y
	sta r0H
	iny
	lda (returnAddress),y
	sta r1L
	iny
	lda (returnAddress),y
	sta r1H
	iny
	lda (returnAddress),y
	sta r2L
	iny
	lda (returnAddress),y
	sta r2H
	jsr BitmapUp
	lda #7
	jmp k_DoInlineReturn

k_i_FillRam:                    ; .word count, address / .byte value
	TAKE
	lda (returnAddress),y
	sta r0L
	iny
	lda (returnAddress),y
	sta r0H
	iny
	lda (returnAddress),y
	sta r1L
	iny
	lda (returnAddress),y
	sta r1H
	iny
	lda (returnAddress),y
	sta r2L
	jsr FillRam
	lda #6
	jmp k_DoInlineReturn

k_i_MoveData:                   ; .word source, destination, count
	TAKE
	lda (returnAddress),y
	sta r0L
	iny
	lda (returnAddress),y
	sta r0H
	iny
	lda (returnAddress),y
	sta r1L
	iny
	lda (returnAddress),y
	sta r1H
	iny
	lda (returnAddress),y
	sta r2L
	iny
	lda (returnAddress),y
	sta r2H
	jsr MoveData
	lda #7
	jmp k_DoInlineReturn

; .word x / .byte y / the string, 0: continue after the 0
k_i_PutString:
	TAKE
	lda (returnAddress),y
	sta r11L
	iny
	lda (returnAddress),y
	sta r11H
	iny
	lda (returnAddress),y
	sta r1H
	lda returnAddress       ; r0 = the string
	clc
	adc #4
	sta r0L
	lda returnAddress+1
	adc #0
	sta r0H
	jsr PutString
	jmp AfterR0

; the string follows; continue after its 0
k_i_GraphicsString:
	TAKE
	lda returnAddress
	clc
	adc #1
	sta r0L
	lda returnAddress+1
	adc #0
	sta r0H
	jsr GraphicsString
AfterR0:
	; r0 = past the string's 0: continue there
	lda r0L
	sta returnAddress
	lda r0H
	sta returnAddress+1
	jmp (returnAddress)

; returnAddress += a, continue there (a = parameter bytes + 1)
k_DoInlineReturn:
	clc
	adc returnAddress
	sta returnAddress
	bcc @1
	inc returnAddress+1
@1:	jmp (returnAddress)
