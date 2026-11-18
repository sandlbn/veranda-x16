; Veranda Solitaire: drawing straight into VERA's foreground (VRAM $00000,
; 80 bytes a row, 1 = ink; Veranda keeps the screen there at 640x480 and
; the program draws to the foreground only, DECISIONS V5).
;
; bl_card  a card's picture from a RAM bank: columns of 80 bytes, VERA's
;          address stepping by 80 (one store a row); the two side columns
;          merge the screen under the rounded corners (masks from
;          tools/mkcards.py), read through port 1
; bl_fill  the table's pattern (sparse dots) over an area of whole bytes
; bl_xor   one row or one column of a frame, inverted (the dragged outline)
;
; The interrupt keeps VERA's ports and CTRL (DECISIONS V8), so these run
; between anything. GPL-3.0-or-later (see COPYING).

	.export _bl_card, _bl_fill, _bl_xorh, _bl_xorv
	.export _bl_bank, _bl_src, _bl_x, _bl_y, _bl_r0, _bl_r1, _bl_c0, _bl_c1, _bl_w, _bl_h
	.export _bl_x0, _bl_x1, _bl_pat
	.importzp ptr1, ptr2, tmp1, tmp2, tmp3, tmp4

ADDR_L  = $9F20
ADDR_M  = $9F21
ADDR_H  = $9F22
DATA0   = $9F23
DATA1   = $9F24
CTRL    = $9F25
INC1    = $10
INC80   = $C0
RAMBANK = $00

	.bss
_bl_bank: .res 1        ; the picture's RAM bank
_bl_src:  .res 2        ; its address there
_bl_x:    .res 1        ; the card's left: a byte column (0-79)
_bl_y:    .res 2        ; the screen row of the picture's row 0
_bl_r0:   .res 1        ; the picture's rows [r0, r1)
_bl_r1:   .res 1
_bl_c0:   .res 1        ; its columns [c0, c1)
_bl_c1:   .res 1
_bl_w:    .res 1        ; bl_fill: bytes across
_bl_h:    .res 2        ; bl_fill: rows; bl_xorv: rows
_bl_x0:   .res 2        ; bl_xorh: the first and last pixel; bl_xorv: x0 the pixel
_bl_x1:   .res 2
_bl_pat:  .res 4        ; bl_fill: the pattern, by row & 3
col:      .res 1
rows:     .res 1
base:     .res 2
mrow:     .res 1

	.rodata
c80lo:	.byte <0, <80, <160, <240, <320, <400, <480
c80hi:	.byte >0, >80, >160, >240, >320, >400, >480
; the corners' masks, a byte a row: bits outside the card keep the screen
mask0:	.byte $C0, $80, $00, $00
	.res 72, 0
	.byte $00, $00, $80, $C0
mask6:	.byte $03, $01, $00, $00
	.res 72, 0
	.byte $00, $00, $01, $03

	.code

; a/x = row (16 bits) -> base = row * 80 (row * 16 * 5)
mul80:	sta base
	stx base+1
	asl base
	rol base+1
	asl base
	rol base+1
	asl base
	rol base+1
	asl base
	rol base+1              ; row * 16
	lda base
	sta tmp3
	lda base+1
	sta tmp4
	asl base
	rol base+1
	asl base
	rol base+1              ; row * 64
	clc
	lda base
	adc tmp3
	sta base
	lda base+1
	adc tmp4
	sta base+1
	rts

; VERA port 0 (and port 1 with c set) at base + y, stepping by a (ADDR_H)
setaddr:
	sta tmp1
	bcc @p0
	lda #1
	sta CTRL
	jsr @set
@p0:	stz CTRL
@set:	tya
	clc
	adc base
	sta ADDR_L
	lda base+1
	adc #0
	sta ADDR_M
	lda tmp1
	sta ADDR_H
	rts

_bl_card:
	lda RAMBANK
	pha
	lda _bl_bank
	sta RAMBANK
	stz CTRL
	lda _bl_r1
	sec
	sbc _bl_r0
	beq @out
	sta rows
	clc
	lda _bl_y
	adc _bl_r0
	pha
	lda _bl_y+1
	adc #0
	tax
	pla
	jsr mul80               ; base = (y + r0) * 80
	clc
	lda base
	adc _bl_x
	sta base
	bcc :+
	inc base+1
:	lda _bl_c0
	sta col
@col:	lda col
	cmp _bl_c1
	bcs @out
	tax
	clc                     ; the source: src + col * 80 + r0
	lda _bl_src
	adc c80lo,x
	sta ptr1
	lda _bl_src+1
	adc c80hi,x
	sta ptr1+1
	clc
	lda ptr1
	adc _bl_r0
	sta ptr1
	bcc :+
	inc ptr1+1
:	ldy col
	cpx #0
	beq @side
	cpx #6
	beq @side
	clc                     ; port 0 only
	lda #INC80
	jsr setaddr
	ldy #0
	ldx rows
@row:	lda (ptr1),y
	sta DATA0
	iny
	dex
	bne @row
@next:	inc col
	bra @col
@out:	stz CTRL
	pla
	sta RAMBANK
	rts

	; a side column: the screen kept under the corners
@side:	sec
	lda #INC80
	jsr setaddr
	lda col
	beq :+
	lda #<mask6
	ldx #>mask6
	bra :++
:	lda #<mask0
	ldx #>mask0
:	clc
	adc _bl_r0
	sta ptr2
	txa
	adc #0
	sta ptr2+1
	ldy #0
	ldx rows
@srow:	lda DATA1
	and (ptr2),y
	ora (ptr1),y
	sta DATA0
	iny
	dex
	bne @srow
	bra @next

; bl_fill: bl_w bytes from column bl_x, bl_h rows from bl_y, in bl_pat;
; a row is a jump into 80 stores (4 cycles a byte)
_bl_fill:
	stz CTRL
	lda _bl_w
	beq @no
	cmp #81
	bcs @no
	lda _bl_h
	ora _bl_h+1
	bne @go
@no:	rts
@go:	lda _bl_w               ; the entry: @fend - w * 3
	sta tmp1
	asl a
	clc
	adc tmp1
	sta tmp1
	sec
	lda #<@fend
	sbc tmp1
	sta @jmp+1
	lda #>@fend
	sbc #0
	sta @jmp+2
	lda _bl_y
	ldx _bl_y+1
	jsr mul80
	clc
	lda base
	adc _bl_x
	sta base
	bcc :+
	inc base+1
:	lda _bl_y
	and #3
	sta mrow
	lda #INC1
	sta ADDR_H
@row:	lda base
	sta ADDR_L
	lda base+1
	sta ADDR_M
	ldx mrow
	lda _bl_pat,x
@jmp:	jmp @fend
	.repeat 80
	sta DATA0
	.endrep
@fend:
	clc
	lda base
	adc #80
	sta base
	bcc :+
	inc base+1
:	lda mrow
	inc a
	and #3
	sta mrow
	lda _bl_h
	bne :+
	dec _bl_h+1
:	dec _bl_h
	lda _bl_h
	ora _bl_h+1
	beq @done
	jmp @row
@done:	rts

; bl_xorh: row bl_y, pixels bl_x0 .. bl_x1 inverted
_bl_xorh:
	lda _bl_y
	ldx _bl_y+1
	jsr mul80
	lda _bl_x0+1            ; the first byte: x0 / 8
	lsr a
	lda _bl_x0
	ror a
	lsr a
	lsr a
	sta tmp2
	lda _bl_x1+1
	lsr a
	lda _bl_x1
	ror a
	lsr a
	lsr a
	sta rows                ; the last byte
	ldy tmp2
	sec
	lda #INC1
	jsr setaddr
	lda _bl_x0
	and #7
	tax
	lda lmask,x             ; the first byte's bits
	sta tmp1
	lda _bl_x1
	and #7
	tax
	lda rmask,x
	sta tmp4                ; the last byte's bits
	ldy tmp2
@b:	lda #$FF
	cpy tmp2
	bne :+
	lda tmp1
:	cpy rows
	bne :+
	and tmp4
:	eor DATA1
	sta DATA0
	iny
	cpy rows
	beq @b
	bcc @b
	stz CTRL
	rts

; bl_xorv: column bl_x0, bl_h rows from bl_y inverted
_bl_xorv:
	lda _bl_h
	beq @done
	lda _bl_y
	ldx _bl_y+1
	jsr mul80
	lda _bl_x0+1
	lsr a
	lda _bl_x0
	ror a
	lsr a
	lsr a
	tay
	sec
	lda #INC80
	jsr setaddr
	lda _bl_x0
	and #7
	tax
	lda bitm,x
	ldx _bl_h
@r:	tay
	eor DATA1
	sta DATA0
	tya
	dex
	bne @r
	stz CTRL
@done:	rts

	.rodata
lmask:	.byte $FF, $7F, $3F, $1F, $0F, $07, $03, $01
rmask:	.byte $80, $C0, $E0, $F0, $F8, $FC, $FE, $FF
bitm:	.byte $80, $40, $20, $10, $08, $04, $02, $01
