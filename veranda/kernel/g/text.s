; Veranda bank G: text
;
; Fonts are GEOS fonts (tools/mkfont.py has the format). y is the baseline
; (as in GEOS); a character covers rows y - baselineOffset .. + curHeight - 1
; and is drawn opaque within that box.
;
; Characters are rendered into a strip in RAM (STRIP_W bytes by up to
; STRIP_H rows), bit by bit, with bold, italic and underline applied there;
; the strip goes to the screen one row at a time (fb_span, FBOP_BUF, with
; reverse as the XOR value): one VERA seek per row of a run of characters
; instead of one per row of every character. PutString collects a run until
; the strip is full, the position or the font changes, or reverse changes.
; Outline is drawn plain for now.

.include "kernel.inc"

.import sysFont, fontBank, fontPage ; (resident: menus and dialogs keep them with the font)
.import kBankBase
.import FontRow

.import fb_span, fbRow, fbCol, fbCnt, fbKeepL, fbKeepR, fbOp, fbSel, fbXor
.importzp FBOP_BUF
.import KeepLeft, KeepRight, BitMask, kStride

.export k_LoadCharSet, k_UseSystemFont, k_GetCharWidth, k_GetRealSize
.export k_WPutChar, k_WSmallPutChar, k_WPutString

STRIP_W = 32                    ; bytes: 256 pixels a run
STRIP_H = 32                    ; rows: fonts up to 32 lines (with the underline)

.segment "BSS"

strip:	.res STRIP_W * STRIP_H
stOpen:	.res 1                  ; $80: a run is being collected
stX0:	.res 2                  ; screen x of the strip's first byte (a card)
stLeft:	.res 1                  ; first pixel's bit in the strip (0-7)
stBit:	.res 1                  ; next character's bit position in the strip
stBase:	.res 2                  ; the baseline of the run
stRev:	.res 1                  ; $FF: reversed
chW:	.res 1                  ; the character's width (with styles)
chGW:	.res 1                  ; its glyph's width (from the index table)
chSrc:	.res 2                  ; bit position in the font's rows
lastW:	.res 1
yb:	.res 2                  ; baseline (wide)
row:	.res 1
smask:	.res 1
dmask:	.res 1
sbyte:	.res 2
rowSrc:	.res 2
dbit:	.res 1
dbyte0:	.res 1
sShift:	.res 1
sVec:	.res 2
stUL:	.res 1
c0:	.res 1
c1:	.res 1
c2:	.res 1
cnt:	.res 1
italic:	.res 1
; (its index table: IDX_COPY, the last 194 bytes of this bank, written by
; the font service in bank T; tools/kbuild.py checks that bank G ends before)

.segment "CODE"

;---------------------------------------------------------------
; fonts
;---------------------------------------------------------------
k_UseSystemFont:
	LoadW r0, SystemFont
k_LoadCharSet:
	stz fontBank            ; (a font in the program's memory)
	stz sysFont             ; the system font has a fast path (SysRows)
	lda r0L
	cmp #<SystemFont
	bne @g
	lda r0H
	cmp #>SystemFont
	bne @g
.ifndef NOFASTFONT
	dec sysFont
.endif
@g:	ldy #0
	lda (r0),y
	sta baselineOffset
	iny
	lda (r0),y
	sta curSetWidth
	iny
	lda (r0),y
	sta curSetWidth+1
	iny
	lda (r0),y
	sta curHeight
	iny
	lda (r0),y
	clc
	adc r0L
	sta curIndexTable
	iny
	lda (r0),y
	adc r0H
	sta curIndexTable+1
	iny
	lda (r0),y
	clc
	adc r0L
	sta cardDataPntr
	iny
	lda (r0),y
	adc r0H
	sta cardDataPntr+1
	rts

; a = character -> a = its width in the font (0 for codes)
k_GetCharWidth:
	sec
	sbc #32
	bcc @0
	cmp #96
	bcc @1
	lda #95                 ; past 127: as 127
@1:	asl
	tay
	iny
	iny
	lda (curIndexTable),y   ; next start - this start
	dey
	dey
	sec
	sbc (curIndexTable),y
	rts
@0:	lda #0
	rts

; a = a glyph's width, x = currentMode -> a = the width with the styles
; (bold + 1, outline + 2)
Styled:	pha
	txa
	and #SET_BOLD
	beq @1
	pla
	inc a
	pha
@1:	txa
	and #SET_OUTLINE
	beq @2
	pla
	inc a
	inc a
	rts
@2:	pla
	rts

; a = character, x = currentMode -> y = width, x = height, a = baseline
k_GetRealSize:
	stx kTmp
	jsr k_GetCharWidth
	tay
	ldx curHeight
	lda kTmp
	and #SET_BOLD
	beq @1
	iny
@1:	lda kTmp
	and #SET_OUTLINE
	beq @2
	iny
	iny
	inx
	inx
	lda baselineOffset
	clc
	adc #2
	rts
@2:	lda baselineOffset
	rts

;---------------------------------------------------------------
; WPutChar: a, r11 x (advances), r12 baseline
;---------------------------------------------------------------
k_WPutChar:
	pha
	MoveW r12, yb
	pla
	jsr PutC
	jmp Flush

; without codes and margins
k_WSmallPutChar:
	pha
	MoveW r12, yb
	pla
	cmp #32
	bcc @r
	jsr AddChar
	jmp Flush
@r:	rts

; one character of a string (codes included): the run stays open
PutC:	cmp #32
	bcs @ch
	jmp Code
@ch:	pha
	jsr k_GetCharWidth      ; the glyph's width, and with the styles
	sta chGW
	ldx currentMode
	cpx #0
	beq @w0
	jsr Styled
@w0:	sta chW
	sta lastW
	tay
	; right edge past rightMargin: StringFaultVec
	tya
	clc
	adc r11L
	sta kTmp
	lda r11H
	adc #0
	sta kTmp+1
	lda kTmp                ; kTmp = x + width - 1
	bne @d
	dec kTmp+1
@d:	dec kTmp
	CmpW rightMargin, kTmp
	bcs @in
	jmp PastR               ; (right of rightMargin: LATE)
@in:	CmpW r11, leftMargin    ; left of leftMargin: skipped
	bcs @draw
	jmp PastL
@draw:	pla
	jmp AddCharW            ; (chW is known)

;---------------------------------------------------------------
; WPutString: r0 string, r11 x, r12 baseline
;---------------------------------------------------------------
k_WPutString:
	MoveW r12, yb
@next:	ldy #0
	lda (r0),y
	beq @end
	IncW r0
	cmp #ESC_GRAPHICS
	beq @gr
	jsr PutC
	bra @next
@end:	IncW r0                 ; r0 past the 0 (i_PutString, ESC_GRAPHICS)
	jmp Flush
@gr:	jsr PutC                ; (V91) GEOS 64's rule: the GraphicsString's
	jmp Flush               ; 0 ends the string too, r0 past it

;---------------------------------------------------------------
; the run
;---------------------------------------------------------------
; a = a character >= 32 at r11, yb: into the strip; r11 advances
AddChar:
	pha
	jsr k_GetCharWidth
	sta chGW
	ldx currentMode
	beq @w0
	jsr Styled
@w0:	sta chW
	sta lastW
	pla
AddCharW:                       ; (chW and chGW are known)
	pha
	; a new run if none is open, the baseline moved, or it is full
	bit stOpen
	bpl @new
	CmpW yb, stBase
	bne @flush
	lda stRev
	eor currentMode         ; (reverse is bit 5)
	and #SET_REVERSE
	bne @flush
	lda stBit
	clc
	adc chW
	bcs @flush
	cmp #256 - 40           ; room for italic and the spill bytes
	bcs @flush
	bra @add
@flush:	jsr Flush
@new:	lda r11L
	and #7
	sta stLeft
	sta stBit
	lda r11L
	and #$F8
	sta stX0
	MoveB r11H, stX0+1
	MoveW yb, stBase
	lda currentMode
	and #SET_REVERSE
	beq @nr
	lda #$FF
@nr:	sta stRev
	LoadB stOpen, $80
@add:	pla
	jsr Glyph
	lda stBit
	clc
	adc chW
	sta stBit
	lda chW
	clc
	adc r11L
	sta r11L
	bcc @r
	inc r11H
@r:	rts

; character a (>= 32) into the strip at stBit, with bold, italic, underline.
; Each row in chunks of up to 8 pixels: a 16-bit window of the font row is
; shifted left until the chunk is its top byte, masked to the chunk's
; width, made bold (v | v >> 1, the extra pixel spills into the next
; byte), shifted right into place (24 bits) and ORed into the strip.
Glyph:	sec
	sbc #32
	cmp #96
	bcc @1
	lda #95
@1:	bit sysFont             ; the system font, no bold or italic: fast
	bpl @slow
	pha
	lda currentMode
	and #SET_BOLD | SET_ITALIC
	bne @sl
	pla
	jmp GlyphFast
@sl:	pla
@slow:	asl
	tay
	lda (curIndexTable),y
	sta chSrc
	iny
	lda (curIndexTable),y
	sta chSrc+1
	; the character's first byte in row 0: cardDataPntr + chSrc / 8
	lsr
	sta kTmp+1
	lda chSrc
	ror
	lsr kTmp+1
	ror
	lsr kTmp+1
	ror
	clc
	adc cardDataPntr
	sta rowSrc
	lda kTmp+1
	adc cardDataPntr+1
	sta rowSrc+1
	lda chSrc
	and #7
	sta sShift
	jsr UiFast              ; (V81: the UI font, c set: drawn)
	bcc @gen
	rts
@gen:	lda #0
	jsr StripRow            ; kPtr = row 0 of the strip
	stz row
@row:	lda row
	cmp curHeight
	bcc @r1
	jmp @ul
@r1:	cmp #STRIP_H
	bcc @r2
	jmp @ul
@r2:	; destination bit: stBit, italic: + (baseline + 2 - row) / 2
	lda stBit
	sta dbit
	lda currentMode
	and #SET_ITALIC
	beq @ni
	lda baselineOffset      ; (baseline + 2 - row) / 2, as GEOS
	clc
	adc #2
	sec
	sbc row
	bcc @ni
	lsr
	clc
	adc dbit
	sta dbit
@ni:	jsr RowPtr              ; kPtr2 = the glyph's row (V62: or a copy)
	lda chGW
	sta cnt                 ; pixels left
	bne @chunk
	jmp @next
@chunk:	; the window, shifted left by sShift
	lda (kPtr2)
	sta c0
	ldy #1
	lda (kPtr2),y
	sta c1
	ldx sShift
	beq @s1
@s0:	asl c1
	rol c0
	dex
	bne @s0
@s1:	; mask to min(8, cnt) pixels
	lda cnt
	cmp #8
	bcc @m
	lda #8
@m:	tax
	lda c0
	and LeadMask,x
	sta c0
	stz c1
	stz c2
	lda currentMode         ; bold
	and #SET_BOLD
	beq @nb
	lda c0
	lsr
	ror c1
	ora c0
	sta c0
@nb:	; into place: right by dbit AND 7
	lda dbit
	and #7
	tax
	beq @p1
@p0:	lsr c0
	ror c1
	ror c2
	dex
	bne @p0
@p1:	lda dbit
	lsr
	lsr
	lsr
	tay
	lda c0
	ora (kPtr),y
	sta (kPtr),y
	iny
	lda c1
	ora (kPtr),y
	sta (kPtr),y
	lda c2
	beq @p2
	iny
	ora (kPtr),y
	sta (kPtr),y
@p2:	; the next chunk: 8 pixels on
	inc kPtr2
	bne @p3
	inc kPtr2+1
@p3:	lda dbit
	clc
	adc #8
	sta dbit
	lda cnt
	sec
	sbc #8
	bcc @next
	beq @next
	sta cnt
	jmp @chunk
@next:	AddW curSetWidth, rowSrc
	AddVW STRIP_W, kPtr
	inc row
	jmp @row
@ul:
Underline:
	; underline: the row below the baseline, the character's full width
	lda currentMode
	and #SET_UNDERLINE
	beq @r
	LoadB stUL, $80
	lda baselineOffset
	clc
	adc #1
	cmp #STRIP_H
	bcs @r
	jsr StripRow
	lda stBit
	sta dbit
	lda chW
	sta cnt
@u:	jsr SetBit
	inc dbit
	dec cnt
	bne @u
@r:	rts
	; (GlyphFast's row loop stays in one page: V62, V81; the assert below)

; the system font, plain: its rows left aligned (SysRows, 9 a character),
; each shifted right once by stBit AND 7 and ORed into two strip bytes
GlyphFast:
	sta kTmp                ; kPtr2 = SysRows + index * 9
	stz kTmp+1
	asl
	rol kTmp+1
	asl
	rol kTmp+1
	asl
	rol kTmp+1
	clc
	adc kTmp
	sta kPtr2
	lda kTmp+1
	adc #0
	sta kPtr2+1
	AddVW SysRows, kPtr2
	lda #SYS_H
GlyphRun:                       ; (a = rows, kPtr2 their left aligned bytes: UiFast too)
	sta row
	lda #0
	jsr StripRow
	lda stBit
	and #7
	sta sShift
	lda stBit
	lsr
	lsr
	lsr
	sta dbyte0
	; the shift by sShift: a jump into the unrolled chain below
	lda sShift
	asl
	asl
	sta kTmp
	lda #<ChainEnd
	sec
	sbc kTmp
	sta sVec
	lda #>ChainEnd
	sbc #0
	sta sVec+1
FRow:	lda (kPtr2)
	sta kTmp+2
	stz kTmp+3
	jmp (sVec)
Chain:
.repeat 7
	lsr kTmp+2
	ror kTmp+3
.endrepeat
ChainEnd:
	ldy dbyte0
	lda kTmp+2
	ora (kPtr),y
	sta (kPtr),y
	iny
	lda kTmp+3
	ora (kPtr),y
	sta (kPtr),y
	inc kPtr2
	bne @2
	inc kPtr2+1
@2:	lda kPtr
	clc
	adc #STRIP_W
	sta kPtr
	bcc @3
	inc kPtr+1
@3:	dec row
	bne FRow
.assert >FRow = >*, lderror, "GlyphFast's row loop crosses a page (a cycle a row more)"
	jmp Underline

; a = row -> kPtr = strip + row * STRIP_W
StripRow:
	stz kPtr+1
	asl
	asl
	asl
	asl
	rol kPtr+1
	asl
	rol kPtr+1
	clc
	adc #<strip
	sta kPtr
	lda kPtr+1
	adc #>strip
	sta kPtr+1
	rts

; the UI font (bank T), not italic, its width 1-8 (bold: 1-7): bank T
; puts its rows left aligned (shifted, masked, bold) on the stack (LK_ROWS)
; and the system font's loop (GlyphRun) takes them; c set when drawn
; (V81: 0.4 ms a character, from 0.9: no row copy and no bit loops a row)
UiFast:	lda fontBank
	sec
	sbc kBankBase
	cmp #BANK_T
	bne @no
	lda currentMode
	and #SET_ITALIC
	bne @no
	lda chGW
	beq @no
	bit currentMode         ; (bold, bit 6: a pixel wider)
	bvc @w
	ina
@w:	cmp #9
	bcs @no
	tsx                     ; curHeight bytes on the stack
	txa
	sec
	sbc curHeight
	tax
	txs
	inx
	stx kPtr
	LoadB kPtr+1, $01
	lda rowSrc              ; (an offset in the font's bank: from $A000)
	sta kPtr2
	lda rowSrc+1
	ora #$A0
	sta kPtr2+1
	MoveB sShift, kTmp
	MoveB chGW, kTmp+1
	ldx #LK_ROWS
	FARCALL Look
	MoveW kPtr, kPtr2
	lda curHeight
	jsr GlyphRun            ; (its Underline returns here)
	tsx
	txa
	clc
	adc curHeight
	tax
	txs
	sec
	rts
@no:	clc
	rts

; kPtr2 = the glyph's row rowSrc; a banked font (DECISIONS V62): a copy
; through FontRow, (width + 7) / 8 + 1 bytes (the chunks read a byte past
; each). Out of Glyph's way: GlyphFast, the system font's path, keeps its
; addresses, so its loops keep their places in their pages (a branch
; across a page costs a cycle: 1.4 us a character)
.segment "LATE"

; PutC's character (pushed) past rightMargin: StringFaultVec; with the
; clip (V81) drawn if it starts in it (its spans clipped), else skipped
PastR:	lda clipOn
	beq @fault
	CmpW rightMargin, r11
	bcs PDraw
	jsr Flush
	bra PSkip
@fault:	pla
	jsr Flush
	lda StringFaultVec
	ora StringFaultVec+1
	beq PRts
	jmp (StringFaultVec)
; left of leftMargin: skipped; with the clip drawn if it ends in it
PastL:	lda clipOn
	beq PSkip
	CmpW kTmp, leftMargin
	bcs PDraw
PSkip:	pla
	lda chW
	clc
	adc r11L
	sta r11L
	bcc PRts
	inc r11H
PRts:	rts
PDraw:	pla
	jmp AddCharW

; codes 0-31
Code:	cmp #8
	bcc @r
	cmp #28
	bcs @r
	pha
	jsr Flush
	pla
	sec
	sbc #8
	asl
	tax
	jmp (CodeTab,x)
@r:	rts

CodeTab:
	.word CBack, CTab, CLF, CHome, CUp, CCR, CULOn, CULOff
	.word CGraph, CRuler, CRevOn, CRevOff, CGotoX, CGotoY, CGotoXY, CNewSet
	.word CBold, CItal, COutl, CPlain

CBack:	lda r11L                ; back by the last width
	sec
	sbc lastW
	sta r11L
	bcs @1
	dec r11H
@1:	rts
CTab:	lda #' '
	jmp PutC
CLF:	lda curHeight
	clc
	adc yb
	sta yb
	bcc @1
	inc yb+1
@1:	rts
CHome:	MoveW leftMargin, r11
	lda windowTopW
	clc
	adc baselineOffset
	sta yb
	lda windowTopW+1
	adc #0
	sta yb+1
	rts
CUp:	lda yb
	sec
	sbc curHeight
	sta yb
	bcs @1
	dec yb+1
@1:	rts
CCR:	MoveW leftMargin, r11
	jmp CLF
CULOn:	lda #SET_UNDERLINE
	bra SetM
CULOff:	lda #<~SET_UNDERLINE
	bra ClrM
CRevOn:	lda #SET_REVERSE
	bra SetM
CRevOff: lda #<~SET_REVERSE
	bra ClrM
CBold:	lda #SET_BOLD
	bra SetM
CItal:	lda #SET_ITALIC
	bra SetM
COutl:	lda #SET_OUTLINE
SetM:	ora currentMode
	sta currentMode
	rts
ClrM:	and currentMode
	sta currentMode
	rts
CPlain:	stz currentMode
	rts
CGraph:	; a GraphicsString follows (bank G2): r0 points at it, and after it
	; when it ends
	FARCALL k_GraphicsString
	rts
CRuler:	rts                     ; documents only
CGotoX:	jsr Word
	sta r11L
	stx r11H
	rts
CGotoY:	jsr Byte
	sta yb
	stz yb+1
	rts
CGotoXY: jsr CGotoX
	jmp CGotoY
CNewSet: jsr Word
	sta kPtr
	stx kPtr+1
	PushW r0
	MoveW kPtr, r0
	jsr k_LoadCharSet
	PopW r0
	rts

; the next byte(s) of the string
Byte:	ldy #0
	lda (r0),y
	pha
	IncW r0
	pla
	rts
Word:	jsr Byte
	pha
	jsr Byte
	tax
	pla
	rts


.segment "LATE"
RowPtr:	lda fontBank
	bne @b
	MoveW rowSrc, kPtr2
	rts
@b:	lda chGW
	clc
	adc #7
	ror
	lsr
	lsr
	ina
	cmp #ROWBUF
	bcc @1
	lda #ROWBUF
@1:	tay
	lda rowSrc
	ldx rowSrc+1
	jmp FontRow

.segment "CODE"

; set bit dbit of the strip row kPtr (dbit < 256)
SetBit:	phy
	lda dbit
	lsr
	lsr
	lsr
	tay
	lda dbit
	and #7
	tax
	lda BitMask,x
	ora (kPtr),y
	sta (kPtr),y
	ply
	rts

;---------------------------------------------------------------
; the strip to the screen, then cleared
;---------------------------------------------------------------
Flush:	bit stOpen
	bmi @1
	rts
@1:	stz stOpen
	lda stBit
	cmp stLeft
	bne @2
	rts                     ; nothing in it
@2:	; bytes: (stBit + 7) / 8; keep masks for the first and last pixel
	clc
	adc #7
	ror                     ; (c from the add: up to 263)
	lsr
	lsr
	sta fbCnt
	ldx stLeft
	lda KeepLeft,x
	sta fbKeepL
	lda stBit
	sec
	sbc #1
	and #7
	tax
	lda KeepRight,x
	sta fbKeepR
	lda stX0+1              ; first column = stX0 / 8
	lsr
	sta kTmp
	lda stX0
	ror
	lsr kTmp
	ror
	lsr
	sta fbCol
	; clip to the screen's width
	lda kStride             ; columns on the screen
	sec
	sbc fbCol
	bcc @off
	bne @on
@off:	jmp @clear
@on:	cmp fbCnt
	bcs @w
	sta fbCnt
	stz fbKeepR
@w:	MoveB stRev, fbXor
	LoadB fbOp, FBOP_BUF
	MoveB dispBufferOn, fbSel
	; rows: top = baseline - baselineOffset
	lda stBase
	sec
	sbc baselineOffset
	sta kTmp
	lda stBase+1
	sbc #0
	sta kTmp+1
	stz row
@row:	lda row
	cmp curHeight
	bcs @clear
	cmp #STRIP_H
	bcs @clear
	; on the screen and inside the window?
	lda kTmp+1
	bmi @skip
	CmpW kTmp, windowTopW
	bcc @skip
	CmpW windowBottomW, kTmp
	bcc @skip
	CmpW kTmp, screenHeight
	bcs @skip
	MoveW kTmp, fbRow
	lda row
	jsr StripRow
	MoveW kPtr, fbBufPtr
	lda fbCnt
	pha
	jsr fb_span
	pla
	sta fbCnt
@skip:	IncW kTmp
	inc row
	bra @row
@clear:	; clear the bytes used, every row
	lda stBit
	clc
	adc #7
	ror
	lsr
	lsr
	sta cnt
	stz row
@c1:	lda row
	cmp #STRIP_H
	bcs @r
	ldx curHeight           ; rows 0 .. curHeight - 1 (+ the underline)
	bit stUL
	bpl @c0
	inx
	inx
@c0:	cpx row
	beq @r
	bcc @r
	jsr StripRow
	ldy cnt
	beq @r
	lda #0
@c2:	dey
	sta (kPtr),y
	bne @c2
	cpy #0
	bne @c2
	inc row
	bra @c1
@r:	stz stUL
	rts

.segment "RODATA"

LeadMask:	.byte $00, $80, $C0, $E0, $F0, $F8, $FC, $FE, $FF

SystemFont:
	.incbin "system.bin"
SysRows:
.ifndef NOFASTFONT
	.incbin "system_rows.bin"
.endif
SYS_H = 9                       ; (tools/mkfont.py)
