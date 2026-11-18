; Veranda bank U: menus and icons
;
; GEOS 64's behaviour (geometry, order, the flash, the fault rules), written
; for Veranda from a description of it (docs/DECISIONS.md V14):
;
; A menu table: top, bottom (bytes), left, right (words), the flags byte
; (bit 7 VERTICAL, bit 6 CONSTRAINED, bit 5 WIDE_MENU, bits 0-4 the item
; count); with WIDE_MENU two more bytes, the high bytes of top and bottom;
; then 5 bytes an item: the text, the type (bit 7 SUB_MENU, bit 6
; DYN_SUB_MENU, else an action), the sub menu, routine or action.
;
; Drawing (the foreground only): the rectangle in pattern 0, a solid
; frame, the items in the system font, the separators (vertical menus: a
; line %01010101 on each inner item boundary, from left + 1 to right - 1;
; horizontal: a vertical line %10101010 from top + 1 to bottom - 1).
; Horizontal items: boundary 0 = left, each next one = the last + 4 + the
; text's width + 4, the text at boundary + 4, baseline top + 3 + the
; font's baseline; vertical: boundary i = top + i * (height + 5), the text
; at left + 4, baseline boundary + 3 + baseline.
;
; The flash of an action (invert, wait selectionFlash, invert, wait,
; invert, the action with a = the item) and of an icon run from the main
; loop (uiTimer, UiWake): the kernel waits without Sleep, whose
; continuation would run with the application's bank mapped.
;
; The Veranda look (DECISIONS V74; uiLook bit 7, LOOK_BAR, a native
; program's choice: classic tables never set it, so GEOS programs keep
; GEOS 64's look): a horizontal main menu is the menu bar: the full width,
; 16 lines (the table's top; a line under it), on palette entry 2's cards
; (the chrome), the Veranda mark at the left (bank T, kernel/t/look.s),
; item 0 in bold (the program's name) 24 pixels in, each item 6 pixels
; from its text on both sides and its limits on 8-pixel cards, so that
; the item open shows on whole cards; the sound and the date and time at
; the right (drawn again each minute: bank T's LK_TICK). Vertical menus ("lists")
; open under their bar item or beside their list item (the table's
; position is not used: only its width), on whole cards, 16 lines an
; item, the text 8 pixels in, no separators. With uiLook's bits 0-3 a
; palette entry, the item chosen or open takes that colour on its cards
; (MenuHi; the colour map, V30) instead of being inverted. The geometry
; is the kernel's, so pointers and clicks are found as before (limL, limH).
;
; What a menu or a dialog covers comes back from: the application's
; RecoverVector if it set one; else the save-under stack if the
; application draws to the foreground only (it was saved when the menu
; opened); else the back screen (RecoverRectangle, as GEOS 64).

.include "kernel.inc"

.export k_DoMenu, k_ReDoMenu, k_DoPreviousMenu, k_GotoFirstMenu, k_RecoverMenu, k_RecoverAllMenus
.export menuState, menuStateEnd, Div8, fState, iconBusy
.export k_DoIcons, k_DefMouse, UiClick, UiFault, UiWake, UiInit, InvertFore, AppCall, Flash, Covers
.global S_k_SetPattern, S_k_WRectangle, S_k_WFrameRectangle, S_k_WVerticalLine, S_k_WHorizontalLine, S_k_UseSystemFont, S_k_WPutString, S_k_WInvertRectangle, S_k_WRecoverRectangle, S_k_WBitmapUp, S_k_WSetMouseRegion, S_k_WStartMouseMode, S_k_MouseOff
.import kMenuRect, uiTimer, uiWake, AppJsr, kAppVec, sysFont, kSndReq
.export UiFont
.import DlgIcon, DlgFiles
.import UnderPush, UnderPop, UnderReset, UnderDrop, uX, uW, uTop, uBot
.global S_Look

LEVELS = 4
MAXITEMS = 31

mTop    = kMenuRect
mBot    = kMenuRect + 2
mLeft   = kMenuRect + 4
mRight  = kMenuRect + 6

F_MENU1 = 1                     ; flash steps (fState)
F_MENU2 = 2
F_ICON  = 3
F_USER  = 4                     ; another part of bank U (dialog boxes): fVec

STY_BAR  = 1                    ; mStyle (V74): the menu bar
STY_LIST = 2                    ; a vertical menu in the Veranda look
CHROME   = 2                    ; the bar's palette entry (sdk theme.h)

.segment "BSS"

menuState:
stackL:	.res LEVELS             ; each level's table
stackH:	.res LEVELS
optTab:	.res LEVELS             ; each level's item (selected, or for the pointer)
saved:	.res LEVELS             ; $FF: its area is on the save-under stack
cov:	.res LEVELS             ; $FF: its cards' colours put aside (ColorCover)
covX:	.res LEVELS             ; and its cards
covW:	.res LEVELS
covY:	.res LEVELS
covH:	.res LEVELS
mFlags:	.res 1                  ; the current level's flags byte
mBase:	.res 1                  ; its first item's offset (7 or 9)
limL:	.res MAXITEMS + 1       ; the item boundaries (x or y)
limH:	.res MAXITEMS + 1
fresh:	.res 1
fState:	.res 1
fVec:	.res 2
iTop:	.res 2                  ; the rectangle to invert (and given to actions)
iBot:	.res 2
et:	.res 1
d8:	.res 1
iLeft:	.res 2
iRight:	.res 2
item:	.res 1
t:	.res 2
fontSave: .res 9 + FONT_STATE + 4 ; $26-$2E, sysFont fontBank fontPage, the margins
dbSave:	.res 1
patSave: .res 2
iconTab: .res 2                 ; the icon table (high byte 0: none)
iconWide: .res 1
iconN:	.res 1
iconBusy: .res 1                ; an icon's flash is running: clicks on icons wait
iconNum: .res 1
iconRout: .res 2
hitNum:	.res 1                  ; the icon pressed
hitRout: .res 2
mStyle:	.res 1                  ; the current level's look (V74): 0 GEOS 64's, STY_BAR, STY_LIST
barR:	.res 2                  ; the bar's right (its last item's end - 1; 0: not drawn yet)
barY:	.res 2                  ; its top
menuStateEnd:
pxL:	.res LEVELS             ; (V74) where each level opens (x, y: Sub)
pxH:	.res LEVELS
pyL:	.res LEVELS
pyH:	.res LEVELS

.segment "CODE"

; SetVideoMode: no menus, icons, flashes, nothing saved
UiInit:	ldx #LK_OFF             ; (V74: no menu bar)
	FARCALL Look
	jsr UnderReset
	stz iconTab+1
	stz iconBusy
	stz fState
	stz uiWake
	stz menuNumber
	ldx #LEVELS - 1
@1:	stz saved,x
	stz cov,x
	dex
	bpl @1
	rts

;---------------------------------------------------------------
; the current level's table and rectangle
;---------------------------------------------------------------
; kPtr = the table of level menuNumber; kMenuRect, mFlags, mBase; the
; pointer's region (the menu if CONSTRAINED, else the screen)
Load:	ldx menuNumber
	lda stackL,x
	sta kPtr
	lda stackH,x
	sta kPtr+1
	ldy #0
	lda (kPtr),y
	sta mTop
	iny
	lda (kPtr),y
	sta mBot
	iny
	lda (kPtr),y
	sta mLeft
	iny
	lda (kPtr),y
	sta mLeft+1
	iny
	lda (kPtr),y
	sta mRight
	iny
	lda (kPtr),y
	sta mRight+1
	iny
	lda (kPtr),y
	sta mFlags
	stz mTop+1
	stz mBot+1
	ldx #7
	and #WIDE_MENU
	beq @c
	iny
	lda (kPtr),y
	sta mTop+1
	iny
	lda (kPtr),y
	sta mBot+1
	ldx #9
@c:	stx mBase
	jsr Look                ; (V74)
	bit mFlags
	bvc @all
	MoveW mLeft, r3
	MoveW mRight, r4
	MoveW mTop, r11
	MoveW mBot, r12
	jmp S_k_WSetMouseRegion
@all:	FARCALL MouseRegionAll
	rts

;---------------------------------------------------------------
; the Veranda look (V74): mStyle, and the rectangle as it is drawn
;---------------------------------------------------------------
Look:	stz mStyle
	bit uiLook
	bpl @r
	bit mFlags
	bmi List
	lda menuNumber
	bne @r                  ; (a horizontal sub menu: GEOS 64's)
	LoadB mStyle, STY_BAR
	lda mTop                ; 16 lines
	clc
	adc #15
	sta mBot
	lda mTop+1
	adc #0
	sta mBot+1
	lda barR
	ora barR+1
	beq @r
	MoveW barR, mRight      ; (drawn: to its last item)
@r:	rts

; a list: at pxL.. (a sub menu) or at the table's place, on whole cards:
; left and top to a card, the table's width + 8, 16 lines an item; moved
; left if it would leave the screen
List:	LoadB mStyle, STY_LIST
	lda mRight              ; t = the table's width
	sec
	sbc mLeft
	sta t
	lda mRight+1
	sbc mLeft+1
	sta t+1
	ldx menuNumber
	beq @own
	lda pxL,x
	sta mLeft
	lda pxH,x
	sta mLeft+1
	lda pyL,x
	sta mTop
	lda pyH,x
	sta mTop+1
@own:	lda mLeft
	and #$F8
	sta mLeft
	lda mTop
	clc
	adc #7
	and #$F8
	sta mTop
	lda mTop+1
	adc #0
	sta mTop+1
	lda mLeft               ; right = (left + width + 15) & ~7, - 1
	clc
	adc t
	tax
	lda mLeft+1
	adc t+1
	tay
	txa
	clc
	adc #15
	and #$F8
	sta mRight
	tya
	adc #0
	sta mRight+1
	lda mRight
	bne @d
	dec mRight+1
@d:	dec mRight
	CmpW mRight, screenWidth
	bcc @in
	lda mRight              ; past the screen: t = right + 1 - its width
	sec
	sbc screenWidth
	sta t
	lda mRight+1
	sbc screenWidth+1
	sta t+1
	IncW t
	SubW t, mLeft
	SubW t, mRight
@in:	jsr Count               ; bottom = top + 16 x the items
	sta t
	stz t+1
	ldx #4
@x16:	asl t
	rol t+1
	dex
	bne @x16
	lda mTop
	clc
	adc t
	sta mBot
	lda mTop+1
	adc t+1
	sta mBot+1
	rts

; the number of items
Count:	lda mFlags
	and #%00011111
	rts

; kPtr2 = item a's entry (kPtr = the table again: drawing uses it)
Entry:	ldx menuNumber
	ldy stackL,x
	sty kPtr
	ldy stackH,x
	sty kPtr+1
	sta et
	asl
	asl
	clc
	adc et                  ; * 5 (at most 155)
	clc
	adc mBase
	clc
	adc kPtr
	sta kPtr2
	lda kPtr+1
	adc #0
	sta kPtr2+1
	rts

; the screen rectangle r3, r11, r4, r12 = the menu's
MenuRegs:
	MoveW mLeft, r3
	MoveW mRight, r4
	MoveW mTop, r11
	MoveW mBot, r12
	rts

;---------------------------------------------------------------
; drawing
;---------------------------------------------------------------
Draw:	lda dispBufferOn
	sta dbSave
	LoadB dispBufferOn, ST_WR_FORE
	MoveW curPattern, patSave
	lda #0
	FARCALL k_SetPattern
	jsr MenuRegs
	lda mStyle
	cmp #STY_BAR
	bne @box
	stz r3                  ; (V74) the bar: the full width, a line under
	stz r3+1                ; it, its colours and the mark (bank T)
	lda screenWidth
	sec
	sbc #1
	sta r4
	lda screenWidth+1
	sbc #0
	sta r4+1
	FARCALL k_WRectangle
	ldx #LK_BAR
	FARCALL Look
	bra @pat
@box:	FARCALL k_WRectangle
	jsr MenuRegs
	lda #$FF
	FARCALL k_WFrameRectangle
	lda mStyle
	beq @pat
	jsr Cards               ; (V74) a list: its cards as opened (no
	jsr CardsK              ; item shown in colour)
	lda #0
	jsr HiK
@pat:	MoveW patSave, curPattern
	; the font and the margins kept; the system font, plain
	jsr FontPush
	jsr UiFont
	stz currentMode
	stz leftMargin
	stz leftMargin+1
	lda mRight
	sec
	sbc #1
	sta rightMargin
	lda mRight+1
	sbc #0
	sta rightMargin+1
	lda mStyle
	cmp #STY_BAR
	bne @items
	lda screenWidth         ; (the bar: to the screen's right)
	sta rightMargin
	lda screenWidth+1
	sta rightMargin+1
	; the items
@items:	stz item
	bit mFlags
	bmi @vert
	MoveW mLeft, t          ; horizontal: t = x
@h:	ldx item
	lda t
	sta limL,x
	lda t+1
	sta limH,x
	jsr Count
	cmp item
	bne @h1
	jmp @sep
@h1:	jsr HPad
	jsr Text
	MoveW t, r11
	jsr HTop
	jsr Put
	MoveW r11, t
	jsr HEnd
	inc item
	bra @h
@vert:	MoveW mTop, t           ; vertical: t = y
@v:	ldx item
	lda t
	sta limL,x
	lda t+1
	sta limH,x
	jsr Count
	cmp item
	beq @sep
	jsr Text
	lda #4
	ldx mStyle
	beq @v0
	lda #8
@v0:	clc
	adc mLeft
	sta r11
	lda mLeft+1
	adc #0
	sta r11+1
	lda #3
	ldx mStyle
	beq @v1
	lda #16
	jsr Mid
@v1:	clc
	adc t
	sta r12
	lda t+1
	adc #0
	sta r12+1
	jsr Put
	lda curHeight
	clc
	adc #5
	ldx mStyle
	beq @v2
	lda #16
@v2:	clc
	adc t
	sta t
	bcc @v3
	inc t+1
@v3:	inc item
	bra @v
@sep:	lda mStyle
	cmp #STY_BAR
	bne @fp
	lda t                   ; (V74) the bar: to its last item; the sound,
	sec                     ; the date and time at its right (bank T)
	sbc #1
	sta barR
	sta mRight
	lda t+1
	sbc #0
	sta barR+1
	sta mRight+1
	MoveW mTop, barY
	jsr Clock
@fp:	jsr FontPop
	lda mStyle
	bne @done               ; (V74: no separators)
	; the separators, items 1 .. n - 1
	LoadB item, 1
@s:	jsr Count
	cmp item
	beq @done
	bcc @done
	ldx item
	bit mFlags
	bmi @sv
	lda limL,x              ; horizontal menu: a vertical line
	sta r4
	lda limH,x
	sta r4+1
	lda mTop
	clc
	adc #1
	sta r11
	lda mTop+1
	adc #0
	sta r11+1
	lda mBot
	sec
	sbc #1
	sta r12
	lda mBot+1
	sbc #0
	sta r12+1
	lda #%10101010
	FARCALL k_WVerticalLine
	bra @s1
@sv:	lda limL,x              ; vertical menu: a horizontal line
	sta r11
	lda limH,x
	sta r11+1
	lda mLeft
	clc
	adc #1
	sta r3
	lda mLeft+1
	adc #0
	sta r3+1
	lda mRight
	sec
	sbc #1
	sta r4
	lda mRight+1
	sbc #0
	sta r4+1
	lda #%01010101
	FARCALL k_WHorizontalLine
@s1:	inc item
	bra @s
@done:	lda dbSave
	sta dispBufferOn
	rts

; the font, the style and the margins kept (fontSave) and put back
FontPush:
	ldx #8
@fs:	lda baselineOffset,x
	sta fontSave,x
	dex
	bpl @fs
	ldx #FONT_STATE - 1
@fk:	lda sysFont,x
	sta fontSave+9,x
	dex
	bpl @fk
	MoveW leftMargin, fontSave+9+FONT_STATE
	MoveW rightMargin, fontSave+11+FONT_STATE
	rts

FontPop:
	ldx #8
@fr:	lda fontSave,x
	sta baselineOffset,x
	dex
	bpl @fr
	ldx #FONT_STATE - 1
@fb:	lda fontSave+9,x
	sta sysFont,x
	dex
	bpl @fb
	MoveW fontSave+9+FONT_STATE, leftMargin
	MoveW fontSave+11+FONT_STATE, rightMargin
	lda #0                  ; a banked font: its index table (V62)
	FARCALL FontSel
	rts

; (V74) a horizontal item's text: t + 4 (GEOS 64), on the bar t + 6, item
; 0 24 (after the mark) and bold
HPad:	ldx #4
	lda mStyle
	beq @a
	ldx #6
	lda item
	bne @a
	ldx #24
	LoadB currentMode, SET_BOLD
@a:	txa
	clc
	adc t
	sta t
	bcc @r
	inc t+1
@r:	rts

; r12 = its top: the menu's + 3 (GEOS 64), on the bar the middle of 15
HTop:	lda #3
	ldx mStyle
	beq @g
	lda #15
	jsr Mid
@g:	clc
	adc mTop
	sta r12
	lda mTop+1
	adc #0
	sta r12+1
	rts

; after the text: t + 4 (GEOS 64), on the bar + 6 up to a card, plain
HEnd:	lda mStyle
	bne @bar
	AddVW 4, t
	rts
@bar:	stz currentMode
	lda t
	clc
	adc #6 + 7
	and #$F8
	sta t
	bcc @r
	inc t+1
@r:	rts

; a = (a - curHeight) / 2, 0 if the font is taller
Mid:	sec
	sbc curHeight
	bcs @1
	lda #0
@1:	lsr
	rts

; the bar's sound and clock (bank T) from barR + 1, barY; the minute kept
Clock:	lda barR
	clc
	adc #1
	sta kTmp
	lda barR+1
	adc #0
	sta kTmp+1
	MoveW barY, kTmp+2
	ldx #LK_CLOCK
	FARCALL Look
	rts

; r0 = item's text
Text:	lda item
	jsr Entry
	ldy #0
	lda (kPtr2),y
	sta r0
	iny
	lda (kPtr2),y
	sta r0+1
	rts

; the text at r11, r12 = its top + 3 - 3 ...: r12 is the top + 3 here; the
; baseline is that + baselineOffset
Put:	lda r12
	clc
	adc baselineOffset
	sta r12
	bcc @1
	inc r12+1
@1:	jmp S_k_WPutString

; the font of menus and dialog boxes: the system font, or uiFont's (a
; native program's choice, V62)
UiFont:	lda uiFont
	beq @sys
	FARCALL k_UseFont
	rts
@sys:	FARCALL k_UseSystemFont
	rts

;---------------------------------------------------------------
; the pointer on item optTab[level] (fresh, or CONSTRAINED), menus and
; the mouse on
;---------------------------------------------------------------
Pointer:
	lda mouseOn
	bpl @1
	ora #SET_ICONSON
@1:	ora #SET_MENUON
	sta mouseOn
	stz mouseVector
	stz mouseVector+1
	stz mouseFaultVec
	stz mouseFaultVec+1
	lda fresh
	bne @place
	bit mFlags
	bvs @place
	lda #0                  ; on, where it is
	jmp S_k_WStartMouseMode
@place:	ldx menuNumber
	lda optTab,x
	tax
	bit mFlags
	bmi @pv
	; horizontal: x = the item's middle, y = the menu's
	lda limL,x
	clc
	adc limL+1,x
	sta r11
	lda limH,x
	adc limH+1,x
	ror
	sta r11+1
	ror r11
	lda mTop
	clc
	adc mBot
	sta r12
	lda mTop+1
	adc mBot+1
	ror
	sta r12+1
	ror r12
	bra @go
@pv:	lda limL,x
	clc
	adc limL+1,x
	sta r12
	lda limH,x
	adc limH+1,x
	ror
	sta r12+1
	ror r12
	lda mLeft
	clc
	adc mRight
	sta r11
	lda mLeft+1
	adc mRight+1
	ror
	sta r11+1
	ror r11
@go:	lda r11
	ora r11+1
	beq @np                 ; x = 0: not moved (GEOS)
	lda #1
	jmp S_k_WStartMouseMode
@np:	lda #0
	jmp S_k_WStartMouseMode

; open level menuNumber (its table in stack): save under, draw, pointer
Open:	jsr Load
	jsr Save
	jsr Cards               ; its cards in the normal colours
	lda mStyle
	cmp #STY_BAR
	beq @bar                ; (V74: the bar keeps its own: not covered)
	jsr Covers
	bra @c
@bar:	lda #0
@c:	ldx menuNumber
	sta cov,x
	lda kTmp
	sta covX,x
	lda kTmp+1
	sta covW,x
	lda kTmp+2
	sta covY,x
	lda kTmp+3
	sta covH,x
	LoadB fresh, $FF
	jsr Draw
	jmp Pointer

; the area of the current level onto the save-under stack, if it is
; recovered that way (see the top)
Save:	ldx menuNumber
	stz saved,x
	lda RecoverVector
	ora RecoverVector+1
	bne @r
	bit dispBufferOn
	bvs @r                  ; the back screen is drawn: RecoverRectangle
	jsr Cards
	jsr UnderPush
	bcs @r
	ldx menuNumber
	dec saved,x
@r:	rts

; uX, uW, uTop, uBot = the menu rectangle's cards and rows
Cards:	lda mLeft
	ldx mLeft+1
	jsr Div8
	sta uX
	lda mRight
	ldx mRight+1
	jsr Div8
	sec
	sbc uX
	ina
	sta uW
	MoveW mTop, uTop
	MoveW mBot, uBot
	rts

; the colour map under uX, uW, uTop, uBot (cards, rows): its colours put
; aside and the cards 0 (kernel/s/color.s); kTmp-kTmp+3 = the cards
; (x, width, y, height); a = $FF if they were (the map is shown)
Covers:	jsr CardsK
	lda #0
	FARCALL ColorCover
	rts

; kTmp-kTmp+3 = the cards of uX, uW, uTop, uBot (x, width, y, height)
CardsK:	lda uX
	sta kTmp
	lda uW
	sta kTmp+1
	lda uTop
	ldx uTop+1
	jsr Div8
	sta kTmp+2
	lda uBot
	ldx uBot+1
	jsr Div8
	sec
	sbc kTmp+2
	ina
	sta kTmp+3
	rts

; level x's colours back (if they were put aside)
Uncover:
	lda cov,x
	beq @r
	stz cov,x
	lda covX,x
	sta kTmp
	lda covW,x
	sta kTmp+1
	lda covY,x
	sta kTmp+2
	lda covH,x
	sta kTmp+3
	lda #1
	FARCALL ColorCover
@r:	rts

;---------------------------------------------------------------
; the calls
;---------------------------------------------------------------
; r0 menu, a item for the pointer
k_DoMenu:
	sta optTab
	stz menuNumber
	lda r0L
	sta stackL
	lda r0H
	sta stackH
	ldx #LEVELS - 1         ; areas saved for open menus: forgotten
@u:	phx                     ; (GEOS recovers nothing here either), their
	jsr Uncover             ; colours back
	plx
	lda saved,x
	beq @u1
	stz saved,x
	phx
	jsr UnderDrop
	plx
@u1:	dex
	bpl @u
	stz fState
	stz uiWake
	stz barR                ; (V74: the bar not drawn yet)
	stz barR+1
	bit uiLook
	bpl @o
	ldx #LK_OFF
	FARCALL Look
@o:	jmp Open

k_ReDoMenu:
	FARCALL k_MouseOff
	jsr Load
	stz fresh
	stz fState
	jsr Draw
	jmp Pointer

k_DoPreviousMenu:
	FARCALL k_MouseOff
	jsr k_RecoverMenu
	dec menuNumber
	jsr Load
	stz fresh
	stz fState
	jsr Draw
	jmp Pointer

k_GotoFirstMenu:
@1:	lda menuNumber
	beq @r
	jsr k_DoPreviousMenu
	bra @1
@r:	rts

; the current level's area back: r2L top, r2H bottom, r3 left, r4 right
; (and r11, r12: the wide top and bottom)
k_RecoverMenu:
	MoveB mTop, r2L
	MoveB mBot, r2H
	jsr MenuRegs
Recover:			; (r3, r4, r11, r12, r2: the area; saved[x] for x)
	ldx menuNumber
	jsr Uncover
	lda RecoverVector
	ora RecoverVector+1
	beq @k
	MoveW RecoverVector, kAppVec
	jmp AppJsr
@k:	ldx menuNumber
	lda saved,x
	beq @back
	stz saved,x
	jsr UnderPop
	rts
@back:	jmp S_k_WRecoverRectangle

k_RecoverAllMenus:
@1:	jsr Load
	jsr k_RecoverMenu
	dec menuNumber
	bpl @1
	stz menuNumber
	jmp Load

;---------------------------------------------------------------
; a press: c set if a menu or an icon took it
;---------------------------------------------------------------
UiClick:
	bit mouseData
	bmi @no                 ; a release: otherPressVec
	bit mouseOn
	bpl @no
	bvc @icons              ; SET_MENUON (bit 6)
	CmpW mouseYPosW, mTop
	bcc @icons
	CmpW mBot, mouseYPosW
	bcc @icons
	CmpW mouseXPos, mLeft
	bcc @icons
	CmpW mRight, mouseXPos
	bcc @icons
	jsr Select
	sec
	rts
@icons:	lda mouseOn
	and #SET_ICONSON
	beq @f
	jsr IconClick
	bcs @r
@f:	jmp DlgFiles            ; a dialog box's file list
@no:	clc
@r:	rts

; the item under the pointer -> item, iTop..iRight
Find:	jsr Count
	tax
@1:	dex
	beq @k                  ; item 0
	bit mFlags
	bmi @v
	lda mouseXPos           ; limit <= x?
	cmp limL,x
	lda mouseXPos+1
	sbc limH,x
	bcs @k
	bra @1
@v:	lda mouseYPosW
	cmp limL,x
	lda mouseYPosW+1
	sbc limH,x
	bcc @1
@k:	stx item
	; the rectangle: from limit k (+ 1 for item 0) to limit k + 1
	lda limL,x
	sta t
	lda limH,x
	sta t+1
	cpx #0
	bne @2
	IncW t
@2:	bit mFlags
	bmi @rv
	MoveW t, iLeft
	lda limL+1,x
	sta iRight
	lda limH+1,x
	sta iRight+1
	lda mTop
	clc
	adc #1
	sta iTop
	lda mTop+1
	adc #0
	sta iTop+1
	lda mBot
	sec
	sbc #1
	sta iBot
	lda mBot+1
	sbc #0
	sta iBot+1
	rts
@rv:	MoveW t, iTop
	lda limL+1,x
	sta iBot
	lda limH+1,x
	sta iBot+1
	lda mLeft
	clc
	adc #1
	sta iLeft
	lda mLeft+1
	adc #0
	sta iLeft+1
	lda mRight
	sec
	sbc #1
	sta iRight
	lda mRight+1
	sbc #0
	sta iRight+1
	rts

; iTop..iRight inverted on the foreground
InvertFore:
	lda dispBufferOn
	pha
	LoadB dispBufferOn, ST_WR_FORE
	MoveW iLeft, r3
	MoveW iRight, r4
	MoveW iTop, r11
	MoveW iBot, r12
	FARCALL k_WInvertRectangle
	pla
	sta dispBufferOn
	rts

;---------------------------------------------------------------
; (V74) the item chosen shown (a = 1) or not (a = 0): in the Veranda look
; with a colour (uiLook's LOOK_HILITE) on its cards, the bar's normal ones
; the chrome, a list's the normal paper (they are covered); else inverted
; (each call: GEOS 64's flash)
;---------------------------------------------------------------
MenuHi:	ldx mStyle
	beq @inv
	tax
	beq @off
	lda uiLook
	and #LOOK_HILITE
	bra @k
@off:	lda mStyle
	cmp #STY_BAR
	bne @0
	lda #CHROME
	bra @k
@0:	lda #0
@k:	pha
	lda iLeft               ; kTmp: the item's cards
	ldx iLeft+1
	jsr Div8
	sta kTmp
	lda iRight
	ldx iRight+1
	sec
	sbc #1
	bcs @2
	dex
@2:	jsr Div8
	sec
	sbc kTmp
	ina
	sta kTmp+1
	lda mStyle              ; the bar's item 0: not the mark's cards
	cmp #STY_BAR
	bne @4
	lda item
	bne @4
	inc kTmp
	inc kTmp
	dec kTmp+1
	dec kTmp+1
@4:	lda iTop
	ldx iTop+1
	jsr Div8
	sta kTmp+2
	lda iBot
	ldx iBot+1
	sec
	sbc #1
	bcs @3
	dex
@3:	jsr Div8
	sec
	sbc kTmp+2
	ina
	sta kTmp+3
	pla
	jsr HiK
	bcc @r
@inv:	jmp InvertFore
@r:	rts

; kTmp's cards in colour a if the look has colours and the map is shown
; (c clear), else c set
HiK:	tax
	lda uiLook
	and #LOOK_HILITE
	beq @no
	txa
	ldx #LK_HI
	FARCALL Look
	rts
@no:	sec
	rts

; where level y opens (Sub; the Veranda look's lists, V74): under a bar
; item (its left, the line under the bar), beside a list's item (its
; right, its top); x kept
Where:	lda mStyle
	cmp #STY_LIST
	beq @side
	lda iLeft
	sta pxL,y
	lda iLeft+1
	sta pxH,y
	lda iBot
	clc
	adc #1
	sta pyL,y
	lda iBot+1
	adc #0
	sta pyH,y
	rts
@side:	lda iRight
	clc
	adc #8
	sta pxL,y
	lda iRight+1
	adc #0
	sta pxH,y
	lda iTop
	and #$F8
	sta pyL,y
	lda iTop+1
	sta pyH,y
	rts

; a press inside the current level (V69: the menu tick, if the settings
; have it)
Select:	LoadB kSndReq, SND_MENU
	FARCALL k_MouseOff
	jsr Find
	lda #1
	jsr MenuHi
	ldx menuNumber
	lda item
	sta optTab,x
	jsr Entry
	ldy #2
	lda (kPtr2),y
	bmi SelSub
	asl
	bmi SelDyn
	; an action: the flash first
	LoadB fState, F_MENU1
Flash:	lda selectionFlash      ; (dialog boxes come here too)
	sta uiTimer
	LoadB uiWake, 1
	rts
SelSub:	ldy #3
	lda (kPtr2),y
	tax
	iny
	lda (kPtr2),y
	bra Sub
SelDyn:	jsr Regs                ; the routine returns the sub menu in r0
	lda item
	jsr AppCall
	lda r0L
	ora r0H
	beq SelR                  ; 0: nothing (the routine sees to it)
	ldx r0L
	lda r0H
Sub:	pha                     ; x/a = the sub menu: the next level
	inc menuNumber
	ldy menuNumber
	jsr Where
	txa
	sta stackL,y
	pla
	sta stackH,y
	lda #0
	sta optTab,y
	jmp Open
SelR:	rts

; for an item's routine: r0 = its address, r1L its type, r8L and r9L the
; item, r2L, r2H, r3, r4 the inverted rectangle (and r11, r12); kAppVec
Regs:	lda item
	jsr Entry
	ldy #2
	lda (kPtr2),y
	sta r1L
	iny
	lda (kPtr2),y
	sta r0L
	sta kAppVec
	iny
	lda (kPtr2),y
	sta r0H
	sta kAppVec+1
	lda item
	sta r8L
	sta r9L
	MoveB iTop, r2L
	MoveB iBot, r2H
	MoveW iLeft, r3
	MoveW iRight, r4
	MoveW iTop, r11
	MoveW iBot, r12
	rts

; DefMouse (API 2; DECISIONS V44): what the main loop does with a button change when the
; program's mouseVector is 0 (the menus and icons, else otherPressVec), for
; a program whose mouseVector passes the change on (GEOS programs that
; chain to the mouseVector they found: GEOS's holds its own handling)
k_DefMouse:
	jsr UiClick
	bcs @r
	MoveW otherPressVec, kAppVec
	jmp AppCall
@r:	rts

; the application's routine kAppVec with a (nothing if 0); x = its low byte
AppCall:
	tay
	lda kAppVec
	ora kAppVec+1
	beq @r
	ldx kAppVec
	tya
	jmp AppJsr
@r:	rts

; the main loop: uiTimer reached 0
UiWake:	stz uiWake
	lda fState
	cmp #F_MENU1
	beq @m1
	cmp #F_MENU2
	beq @m2
	cmp #F_ICON
	beq @ic
	cmp #F_USER
	bne @r
	stz fState
	jmp (fVec)
@m1:	lda #0                  ; normal again, wait
	jsr MenuHi
	LoadB fState, F_MENU2
	jmp Flash
@m2:	stz fState              ; inverted again: the action
	lda #1
	jsr MenuHi
	ldx menuNumber
	lda optTab,x
	sta item
	jsr Regs
	lda item
	jmp AppCall
@ic:	stz fState
	jsr InvertFore
	jmp Service
@r:	rts

;---------------------------------------------------------------
; the pointer left its region: c set if the menus took it
;---------------------------------------------------------------
UiFault:
	lda mouseOn
	and #SET_MSE_ON | SET_MENUON
	cmp #SET_MSE_ON | SET_MENUON
	bne @no
	lda menuNumber
	beq @yes                ; the main menu stays
	lda faultData
	and #SET_OFFMENU
	bne @prev
	bit mFlags
	bpl @h
	lda faultData
	and #SET_OFFTOP
	bne @prev
	bra @c
@h:	lda faultData
	and #SET_OFFLEFT
	bne @prev
@c:	bit mFlags
	bvs @yes                ; CONSTRAINED: the other edges do nothing
@prev:	jsr k_DoPreviousMenu
@yes:	sec
	rts
@no:	clc
	rts

;---------------------------------------------------------------
; icons: a count (bit 7 WIDE_ICONS), the pointer's x (word) and y (byte;
; wide: word), then 8 bytes an icon: the picture (0: an empty slot), x
; (cards), y, width (cards), height, the routine; wide icons 10 bytes:
; those, then the high bytes of y and height
;---------------------------------------------------------------
k_DoIcons:
	MoveW r0, iconTab
	MoveW r0, kPtr
	ldy #0
	lda (kPtr),y
	and #$80
	sta iconWide
	lda (kPtr),y
	and #$7F
	sta iconN
	stz iconNum
@d:	lda iconNum
	cmp iconN
	beq @dr
	jsr Icon
	lda r0L
	ora r0H
	beq @n
	FARCALL k_WBitmapUp
@n:	inc iconNum
	bra @d
@dr:	FARCALL MouseRegionAll
	lda mouseOn
	bmi @on
	and #<~SET_MENUON
@on:	ora #SET_ICONSON
	sta mouseOn
	stz mouseVector
	stz mouseVector+1
	stz mouseFaultVec
	stz mouseFaultVec+1
	stz iconBusy
	MoveW iconTab, kPtr
	ldy #1
	lda (kPtr),y
	sta r11
	iny
	lda (kPtr),y
	sta r11+1
	iny
	lda (kPtr),y
	sta r12
	stz r12+1
	bit iconWide
	bpl @p
	iny
	lda (kPtr),y
	sta r12+1
@p:	lda r11
	ora r11+1
	beq @np
	lda #1
	jmp S_k_WStartMouseMode
@np:	lda #0
	jmp S_k_WStartMouseMode

; icon iconNum: r0 picture, r1L x, r2L width, r11 y, r12 height (WBitmapUp),
; iconRout; kPtr2 = its entry
Icon:	lda iconNum
	asl
	asl
	asl                     ; * 8
	ldx iconWide
	beq @1
	clc
	adc iconNum
	clc
	adc iconNum             ; * 10
@1:	clc
	adc #4
	bit iconWide
	bpl @2
	clc
	adc #1                  ; (the wide header has a word y)
@2:	clc
	adc iconTab
	sta kPtr2
	lda iconTab+1
	adc #0
	sta kPtr2+1
	ldy #0
	lda (kPtr2),y
	sta r0L
	iny
	lda (kPtr2),y
	sta r0H
	iny
	lda (kPtr2),y
	sta r1L
	iny
	lda (kPtr2),y
	sta r11
	iny
	lda (kPtr2),y
	sta r2L
	iny
	lda (kPtr2),y
	sta r12
	iny
	lda (kPtr2),y
	sta iconRout
	iny
	lda (kPtr2),y
	sta iconRout+1
	stz r11+1
	stz r12+1
	bit iconWide
	bpl @r
	iny
	lda (kPtr2),y
	sta r11+1
	iny
	lda (kPtr2),y
	sta r12+1
@r:	rts

; a press with icons on: c set if an icon took it
IconClick:
	lda iconTab+1
	beq @no
	lda mouseXPos           ; the pointer's card
	ldx mouseXPos+1
	jsr Div8
	sta t
	stz iconNum
@s:	lda iconNum
	cmp iconN
	beq @no
	jsr Icon
	lda r0L
	ora r0H
	beq @next
	lda t                   ; x <= card < x + width
	cmp r1L
	bcc @next
	lda r1L
	clc
	adc r2L
	cmp t
	beq @next
	bcc @next
	CmpW mouseYPosW, r11    ; y <= pointer < y + height
	bcc @next
	lda r11
	clc
	adc r12
	sta r12
	lda r11+1
	adc r12+1
	sta r12+1
	CmpW mouseYPosW, r12
	bcc @hit
@next:	inc iconNum
	bra @s
@no:	clc
	rts
@hit:	lda iconBusy
	bne @took               ; a flash is running: the press waits
	MoveB iconNum, hitNum
	MoveW iconRout, hitRout
	; the rectangle: x * 8 .. (x + w) * 8 - 1, y .. y + h - 1
	MoveW r11, iTop
	lda r12
	sec
	sbc #1
	sta iBot
	lda r12+1
	sbc #0
	sta iBot+1
	lda r1L
	jsr Times8
	MoveW t, iLeft
	lda r1L
	clc
	adc r2L
	jsr Times8
	lda t
	sec
	sbc #1
	sta iRight
	lda t+1
	sbc #0
	sta iRight+1
	lda iconSelFlg
	bmi @flash
	asl
	bpl @plain
	jsr InvertFore          ; ST_INVERT: and it stays so
@plain:	jsr Service
@took:	sec
	rts
@flash:	LoadB iconBusy, 1
	jsr InvertFore
	LoadB fState, F_ICON
	jsr Flash
	sec
	rts

; a = (x * 256 + a) / 8 (a card: below 256)
Div8:	stx d8
	lsr d8
	ror
	lsr d8
	ror
	lsr d8
	ror
	rts

; t = a * 8
Times8:	sta t
	stz t+1
	asl t
	rol t+1
	asl t
	rol t+1
	asl t
	rol t+1
	rts

; the icon's routine: r0L the icon, r0H $FF for a double click (within
; 30 frames of the last single one), the rectangle in r2L, r2H, r3, r4
Service:
	lda dblClickCount
	beq @single
	stz dblClickCount
	lda #$FF
	bra @1
@single:
	LoadB dblClickCount, 30
	lda #0
@1:	sta r0H
	MoveB hitNum, r0L
	stz iconBusy
	MoveB iTop, r2L
	MoveB iBot, r2H
	MoveW iLeft, r3
	MoveW iRight, r4
	lda hitRout+1
	bne @app
	lda hitRout             ; 1-6: a dialog box's system icon
	beq @r
	jmp DlgIcon
@app:	MoveW hitRout, kAppVec
	lda hitRout
	ldx hitRout+1
	jmp AppJsr
@r:	rts
