; Veranda shell: a window's shadow on the desktop's picture (overlay 11
; with wall.c; DECISIONS V78, V81): a 50% ink dither ORed into what is
; there in l .. r by t .. b, on the screens dispBufferOn selects (each
; screen read from itself), the dither $AA on even rows and $55 on odd;
; drawn twice it stays the same. With the kernel's clip on (clipOn: the
; scene) only its part in the clip rectangle. In assembly: the C loop
; took some 700 cycles a row (a multiply and a call a row); here a row's
; address steps by 80.
;
; void wall_shade(unsigned int l, unsigned int t, unsigned int r, unsigned int b)
.include "veranda.inc"
.include "macros.inc"
.include "x16hw.inc"

.importzp c_sp, ptr1, tmp1, tmp2, tmp3, tmp4
.import incsp6
.export _wall_shade

.segment "OVL11BSS"
sL:	.res 2
sT:	.res 2
sR:	.res 2
sB:	.res 2
sN:	.res 1                  ; bytes a row
sM0:	.res 1                  ; the first byte's mask, the last's
sM1:	.res 1

.segment "OVERLAY11"

_wall_shade:
	sta sB
	stx sB+1
	ldy #0                  ; the C stack: r, t, l
	lda (c_sp),y
	sta sR
	iny
	lda (c_sp),y
	sta sR+1
	iny
	lda (c_sp),y
	sta sT
	iny
	lda (c_sp),y
	sta sT+1
	iny
	lda (c_sp),y
	sta sL
	iny
	lda (c_sp),y
	sta sL+1
	jsr incsp6
	lda clipOn
	bne @c
	jmp @in
@c:	CmpW sL, leftMargin     ; the clip: l = max(l, leftMargin) ...
	bcs @1
	MoveW leftMargin, sL
@1:	CmpW rightMargin, sR
	bcs @2
	MoveW rightMargin, sR
@2:	CmpW sT, windowTopW
	bcs @3
	MoveW windowTopW, sT
@3:	CmpW windowBottomW, sB
	bcs @4
	MoveW windowBottomW, sB
@4:	CmpW sR, sL             ; nothing left
	bcc @r
	CmpW sB, sT
	bcc @r
	jmp @in
@r:	rts
@in:	; the columns: l / 8 .. r / 8; the edge masks
	lda sL+1
	ldx sL
	jsr Div8
	sta tmp1                ; bx
	lda sR+1
	ldx sR
	jsr Div8
	sec
	sbc tmp1
	ina
	sta sN
	lda sL
	and #7
	tax
	lda #$FF
@m0:	dex
	bmi @m0d
	lsr
	bra @m0
@m0d:	sta sM0                 ; $FF >> (l AND 7)
	lda sR
	and #7
	eor #7
	tax
	lda #$FF
@m1:	dex
	bmi @m1d
	asl
	bra @m1
@m1d:	sta sM1                 ; $FF << (7 - r AND 7)
	; ptr1 = t * 80 + bx: t * 16 + t * 64
	lda sT
	sta ptr1
	lda sT+1
	ldx #4
@s16:	asl ptr1
	rol
	dex
	bne @s16
	sta ptr1+1
	sta tmp4
	lda ptr1
	sta tmp3
	asl tmp3
	rol tmp4
	asl tmp3
	rol tmp4
	lda ptr1
	clc
	adc tmp3
	sta ptr1
	lda ptr1+1
	adc tmp4
	sta ptr1+1
	lda ptr1
	clc
	adc tmp1
	sta ptr1
	bcc @row
	inc ptr1+1
@row:	lda sT                  ; the dither: $AA on even rows, $55 on odd
	lsr
	lda #$AA
	bcc @ev
	lda #$55
@ev:	sta tmp2
	bit dispBufferOn
	bpl @nf
	lda #0                  ; the foreground at ptr1
	ldx ptr1+1
	jsr Shade
@nf:	bit dispBufferOn
	bvc @nb
	lda ptr1+1              ; the back screen at ptr1 + $9600 (past $FFFF: bit 16)
	clc
	adc #$96
	tax
	lda #0
	adc #0
	jsr Shade
@nb:	CmpW sT, sB
	beq @done
	IncW sT
	lda ptr1
	clc
	adc #80
	sta ptr1
	bcc @row
	inc ptr1+1
	bra @row
@done:	rts

; a:x / 8 (a high, x low) -> a
Div8:	sta tmp3
	txa
	lsr tmp3
	ror
	lsr tmp3
	ror
	lsr tmp3
	ror
	rts

; a row at ptr1 low, x (the middle byte), a (bit 16); the dither tmp2:
; port 0 reads, port 1 writes, from the same byte on
Shade:	ora #VERA_INC1
	ldy #1
	sty VERA_CTRL
	ldy ptr1
	sty VERA_ADDR_L
	stx VERA_ADDR_M
	sta VERA_ADDR_H
	stz VERA_CTRL
	sty VERA_ADDR_L
	stx VERA_ADDR_M
	sta VERA_ADDR_H
	ldx sN
	lda tmp2
	and sM0
	dex
	bne @2
	and sM1                 ; one byte: both masks
	ora VERA_DATA0
	sta VERA_DATA1
	rts
@2:	ora VERA_DATA0
	sta VERA_DATA1
	dex
	beq @l
@3:	lda tmp2
	ora VERA_DATA0
	sta VERA_DATA1
	dex
	bne @3
@l:	lda tmp2
	and sM1
	ora VERA_DATA0
	sta VERA_DATA1
	rts
