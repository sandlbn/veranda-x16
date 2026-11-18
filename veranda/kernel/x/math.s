; Veranda bank X: GEOS's maths calls on zero page words, the divisions and the rest (written
; from the GEOS programmer's reference: registers and results as
; documented there; from bank G2, DECISIONS V81: no bank has room for all)
;
; x (and y) are zero page addresses of the operands; words are low byte
; first. Results go to the operand at x.

.include "kernel.inc"

.export k_Ddiv, k_DSdiv, k_Dabs, k_Dnegate, k_Ddec

; the word at x divided by the word at y: the quotient at x, the remainder
; in r8
k_Ddiv:
	lda a:0,y
	sta kTmp
	lda a:1,y
	sta kTmp+1
	stz r8L
	stz r8H
	lda #16
	sta kTmp+2
@l:	asl 0,x
	rol 1,x
	rol r8L
	rol r8H
	lda r8L
	sec
	sbc kTmp
	tay
	lda r8H
	sbc kTmp+1
	bcc @n
	sta r8H
	sty r8L
	inc 0,x
@n:	dec kTmp+2
	bne @l
	rts

; signed: the quotient's sign from both, the remainder's from the dividend
k_DSdiv:
	lda 1,x
	sta kPtr                ; the dividend's sign
	eor a:1,y
	sta kPtr+1              ; the quotient's
	jsr k_Dabs
	phx
	tya
	tax
	jsr k_Dabs
	plx
	jsr k_Ddiv
	bit kPtr+1
	bpl @1
	jsr k_Dnegate
@1:	bit kPtr
	bpl @r
	phx
	ldx #r8
	jsr k_Dnegate
	plx
@r:	rts

; the word at x made positive
k_Dabs:
	lda 1,x
	bpl Done
	; (on into Dnegate)

; the word at x negated
k_Dnegate:
	sec
	lda #0
	sbc 0,x
	sta 0,x
	lda #0
	sbc 1,x
	sta 1,x
Done:	rts

; the word at x minus 1; Z set if it is 0 then
k_Ddec:
	lda 0,x
	bne @1
	dec 1,x
@1:	dec 0,x
	lda 0,x
	ora 1,x
	rts

