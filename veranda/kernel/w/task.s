; Veranda bank W: the switcher (DECISIONS V83)
;
; One full-screen application at a time, the others put aside whole into
; RAM banks, Switcher-style. switchReq (Alt + Tab: bank X's KeyPut;
; TaskCtl TASK_ASIDE) is seen by ClockPoll (bank S) at the main loop's
; top level, where nothing of the application is on the stack, the C
; stack is empty and r0-r15 are dead: TC_POLL puts the application aside
; and starts the desktop (EnterDeskTop), whose dock shows it; TaskCtl
; TASK_RESUME (the dock) ends the caller as GetFile would and brings the
; task back to the top of its main loop.
;
; A task's RAM banks: a chain (the first is its context bank) and its own
; banks, which it keeps (overlays, its open record file, what it asked
; for, the fonts it uses); tb (kernel/w/banks.s) has them all, so that
; BanksInit keeps them used and their fonts never spare.
;
; The context bank:
;   $A000 "VTSK", $A004 the chain's count, $A005 its banks (CHAIN_MAX)
;   CTX   the context (Ctx: zero page, $0400-$07FF, the sleeping routines,
;         the menu rectangle, the font, bank U's timer, the processes, the
;         VERA registers, sprites 0-7, palette entries 0-15, the colour
;         map's state, the name, the bank table)
;   FREC  bank F's record file and the program's overlays (RecVars,
;         LdVars: kgen/kvars.inc)
;   STREAM, and on into the chain's banks: the packed pages (pack.s): the
;         application's RAM, VRAM, bank 1, bank U
; Not kept: what is live (the pointer, the buttons and keys, the clock,
; random), r0-r15, open streams or a listing (such a program is asked to
; quit instead), the PSG, the PCM's FIFO.

.include "kernel.inc"
.include "kvars.inc"

.export k_TaskCtl
.import used, runF, runN, keep, spare, tb, TakeBank, FreeCount, k_BankFree, BankBit
.import PkImgA, PkLenA, PkImgB, PkLenB
.import kBankBase, k_BankCopy, kMenuRect, sysFont, uiTimer, uiWake, SleepN, kAppName
.import k_MainLoop, k_CallRoutine, LogStr, LogNL, IsEmu, kSndReq
.global S_ProcOut, S_ProcIn, S_k_DoDlgBox, S_k_EnterDeskTop, S_k_SetVideoMode, S_FontSel
.global S_k_ChDir, S_AudioReset, S_k_CloseRecordFile, S_k_FClose, S_PrReset

SLOTS   = TASK_MAX
RESERVE = 18                    ; banks left free for the desktop (the shell
				; needs 17, its picture 5 more that it lends)
ZP1     = $3F - $22             ; $22-$3E
ZP2     = $80 - $50             ; $50-$7F
ZP3     = $100 - $A9            ; $A9-$FF
SLEEPBUF_SIZE = 1 + 8 * 4       ; (kernel/res/mainloop.s: SleepN ...)
CH_N    = $A004
CH_LIST = $A005
CTX     = $A030
REC_LEN = F_RecVarsEnd - F_RecVars
LD_LEN  = F_LdVarsEnd - F_LdVars

; a task's record (TaskCtl TASK_INFO gives its first TASK_REC bytes)
R_STATE = TR_STATE
R_BANKS = TR_BANKS
R_NAME  = TR_NAME
R_NOTE  = TR_NOTE
R_CTX   = TASK_REC              ; the context bank
R_OWN   = R_CTX + 1             ; its own banks (32: a bit a bank)
R_ALL   = R_OWN + 32            ; and with the chain
REC     = R_ALL + 32
.assert TR_NOTE + TASK_NOTE_LEN = TASK_REC, error, "TASK_REC"

.segment "BSS"

recs:	.res SLOTS * REC
sl:	.res 1                  ; the task
curFlags: .res 1                ; the running application's info block flags
curDesk: .res 1                 ; $80: it is the desktop
curNote: .res TASK_NOTE_LEN     ; its note
pend:	.res TASK_NOTE_LEN      ; the next one's
lastErr: .res 1                 ; the last start that failed
lastName: .res 17
daImg:	.res 32 + 24 + 24       ; the bank table while an accessory runs
dlgKeep: .res 128               ; dbFileNames under the question's box
why:	.res 1
resv:	.res 1                  ; TakeBank's reserve: RESERVE, or 0 (an accessory)
daChain: .res 1 + CHAIN_MAX     ; an accessory's: PK_N, PK_LIST
own:	.res 32
cnt:	.res 1
tmp:	.res 4
keepP:	.res 3                  ; TbAll: kPtr2, sl
t0:	.res 4                  ; the emulator's cycle counter at the start
; the context, as it goes into the context bank at CTX
Ctx:
cZp:	.res ZP1 + ZP2 + ZP3
cVars:	.res 1024
cSleep:	.res SLEEPBUF_SIZE
cRect:	.res 8
cFont:	.res FONT_STATE
cUi:	.res 2
cProc:	.res 168
cCol:	.res 2                  ; kColorOn, kCov (bank S)
cVid:	.res 1                  ; DC_VIDEO
cLay:	.res 14                 ; the layers
cSpr:	.res 64                 ; sprites 0-7's attributes
cPal:	.res 32                 ; palette entries 0-15
cName:	.res 17
cFlags:	.res 1                  ; its info block's flags
cUsed:	.res 32                 ; the bank table
cRunF:	.res 24
cRunN:	.res 24
CtxEnd:
CTX_LEN = CtxEnd - Ctx
live:	.res 18

FREC    = CTX + CTX_LEN
FLD     = FREC + REC_LEN
STREAM  = (FLD + LD_LEN + 255) & $FF00
.assert STREAM < $B800, error, "the context bank has too little room for the packed data"

.segment "CODE"

;---------------------------------------------------------------
; TaskCtl: a = the operation (veranda.api; >= $80 the kernel's own)
;---------------------------------------------------------------
k_TaskCtl:
	cmp #TC_DAUNPACK + 1
	bcs @bad
	cmp #TC_POLL
	bcs @k
	cmp #TASK_ASIDE + 1
	bcs @bad
	asl
	tay
	lda Ops + 1,y
	pha
	lda Ops,y
	pha
	rts
@k:	and #$7F
	asl
	tay
	lda KOps + 1,y
	pha
	lda KOps,y
	pha
	rts
@bad:	ldx #INV_RECORD
	rts

Ops:	.word Info - 1, Resume - 1, End - 1, Note - 1, Aside - 1
KOps:	.word Poll - 1, Start - 1, DaKeep - 1, DaBack - 1, Fail - 1, Take - 1, DaPack - 1, DaUnpack - 1

; x = the task -> kPtr2 its record, sl; c set if there is no such task
Slot:	cpx #SLOTS
	bcs @r
	stx sl
	LoadW kPtr2, recs
@1:	dex
	bmi @c
	AddVW REC, kPtr2
	bra @1
@c:	clc
@r:	rts

; the task (Slot) put aside? x = 0; else INV_RECORD, z clear
Used:	jsr Slot
	bcs @no
	lda (kPtr2)
	beq @no
	ldx #0
	rts
@no:	ldx #INV_RECORD
	rts

;---------------------------------------------------------------
; TASK_INFO: task x's record to r0 (a = its state); x = $FF: the summary
;---------------------------------------------------------------
Info:	cpx #$FF
	beq @sum
	jsr Slot
	bcs Bad
	ldy #TASK_REC - 1
@c:	lda (kPtr2),y
	sta (r0),y
	dey
	bpl @c
	lda (kPtr2)
	ldx #0
	rts
@sum:	ldx #SLOTS - 1          ; the tasks
	ldy #0
@n:	phx
	phy
	jsr Slot
	ply
	plx
	lda (kPtr2)
	beq @n1
	iny
@n1:	dex
	bpl @n
	tya
	sta (r0)
	jsr FreeCount
	phy
	ldy #TS_FREE
	sta (r0),y
	pla
	iny
	sta (r0),y
	iny
	lda lastErr
	sta (r0),y
	stz lastErr
	ldx #0
@nm:	lda lastName,x
	iny
	sta (r0),y
	inx
	cpx #17
	bne @nm
	ldx #0
	rts
Bad:	ldx #INV_RECORD
	rts

;---------------------------------------------------------------
; TASK_NOTE: r0's text goes with the next application
;---------------------------------------------------------------
Note:	ldy #0
@c:	lda (r0),y
	sta pend,y
	beq @z
	iny
	cpy #TASK_NOTE_LEN - 1
	bne @c
	lda #0
	sta pend,y
@z:	ldx #0
	rts

;---------------------------------------------------------------
; TASK_ASIDE: put aside at the main loop's next pass
;---------------------------------------------------------------
Aside:	lda #SWITCH_ASIDE
	sta switchReq
	ldx #0
	rts

;---------------------------------------------------------------
; TC_START (AppReset): x the flags, y $80 for the desktop
;---------------------------------------------------------------
Start:	stx curFlags
	sty curDesk
	stz switchReq
	stz VERA_CTRL           ; sprites 1-7's pictures cleared (unused
	LoadB VERA_ADDR_L, <(VRAM_SPRITES + $800)  ; slots then pack to
	LoadB VERA_ADDR_M, >(VRAM_SPRITES + $800)  ; nothing)
	LoadB VERA_ADDR_H, ^VRAM_SPRITES | VERA_INC1
	LoadW r0, 7 * $800
	jsr Zeros
	ldy #TASK_NOTE_LEN - 1
@c:	lda pend,y
	sta curNote,y
	lda #0
	sta pend,y
	dey
	bpl @c
	rts

;---------------------------------------------------------------
; TC_FAIL (LdApplic): x the error; the program in dirEntryBuf
;---------------------------------------------------------------
Fail:	stx lastErr
	ldx #0
@c:	lda dirEntryBuf + OFF_FNAME,x
	cmp #$A0
	beq @z
	sta lastName,x
	inx
	cpx #16
	bne @c
@z:	stz lastName,x
	rts

;---------------------------------------------------------------
; TC_TAKE (the pack image): a bank for the chain, RESERVE kept free
;---------------------------------------------------------------
Take:	lda resv
	jmp TakeBank

;---------------------------------------------------------------
; TC_DAPACK, TC_DAUNPACK (kernel/s/desk.s, V83): a desk accessory puts
; the application aside packed (no reserve: the desktop does not start);
; x = 0, or NO_BANKS (nothing taken)
;---------------------------------------------------------------
DaPack:	LoadW r0, S_kColorOn    ; (Tidy: the map shown or not)
	LoadW r1, cCol
	LoadW r2, 2
	jsr FromS
	jsr Tidy
	stz resv
	lda #0
	jsr TakeBank
	cpx #0
	bne @r
	pha
	LoadW r0, PkImgA
	LoadW r2, PkLenA
	jsr Image
	pla
	sta PK_LIST
	LoadB PK_N, 1
	LoadW r4, $A000
	jsr PK_RUN
	ldx #CHAIN_MAX
@c:	lda PK_N,x              ; the chain kept
	sta daChain,x
	dex
	bpl @c
	bcc @ok
	jsr Unchain
	ldx #NO_BANKS
@r:	rts
@ok:	ldx #0
	rts
DaUnpack:
	LoadW r0, PkImgB
	LoadW r2, PkLenB
	jsr Image
	ldx #CHAIN_MAX
@c:	lda daChain,x
	sta PK_N,x
	dex
	bpl @c
	LoadW r4, $A000
	jsr PK_RUN
; the chain's banks (daChain) free
Unchain:
	ldx daChain
@f:	dex
	bmi @r
	phx
	lda daChain + 1,x
	jsr Unuse
	plx
	bra @f
@r:	rts

;---------------------------------------------------------------
; TC_DAKEEP, TC_DABACK (kernel/s/desk.s): the bank table while an
; accessory runs; back: the accessory's banks (x the first) free again,
; the fonts' kept
;---------------------------------------------------------------
DaKeep:	ldx #31
@1:	lda used,x
	sta daImg,x
	dex
	bpl @1
	ldx #23
@2:	lda runF,x
	sta daImg + 32,x
	lda runN,x
	sta daImg + 56,x
	dex
	bpl @2
	rts
DaBack:	phx
	ldx #31
@1:	lda daImg,x
	ora keep,x
	sta used,x
	dex
	bpl @1
	ldx #23
@2:	lda daImg + 32,x
	sta runF,x
	lda daImg + 56,x
	sta runN,x
	dex
	bpl @2
	pla
	jmp k_BankFree

;---------------------------------------------------------------
; TC_POLL (ClockPoll, the main loop's top level, switchReq set): the
; application put aside, the desktop started; or a box asks whether it
; may quit, and the program goes on if not
;---------------------------------------------------------------
Poll:	jsr Clock0
	stz switchReq
	lda #1                  ; streams or a listing open: not now
	sta why
	lda curFlags
	and #APPF_STAY
	bne @ask
	LoadW r0, F_StrOpen     ; (bank F's three streams, and the listing)
	LoadW r1, tmp
	LoadW r2, 3
	jsr FromF
	LoadW r0, F_StrDir
	LoadW r1, tmp+3
	LoadW r2, 1
	jsr FromF
	lda tmp
	ora tmp+1
	ora tmp+2
	ora tmp+3
	bne @ask
	stz why                 ; (not enough memory)
	ldx #SLOTS - 1          ; a free record
@f:	jsr Slot
	lda (kPtr2)
	beq @free
	ldx sl
	dex
	bpl @f
	bra @ask
@free:	LoadB resv, RESERVE
	jsr Keep
	bcc @ok
@ask:	jmp Ask
@ok:	; the record
	ldx sl
	jsr Slot
	lda #TASK_ASIDE
	sta (kPtr2)
	ldy #R_NAME
	ldx #0
@nm:	lda kAppName,x
	sta (kPtr2),y
	iny
	inx
	cpx #17
	bne @nm
	ldx #0
@no:	lda curNote,x
	sta (kPtr2),y
	iny
	inx
	cpx #TASK_NOTE_LEN
	bne @no
	lda PK_LIST
	ldy #R_CTX
	sta (kPtr2),y
	ldx #0                  ; its own banks, all, and the count
	stz cnt
@o:	lda own,x
	iny                     ; (R_OWN + x)
	sta (kPtr2),y
	lda keep,x              ; (the fonts are not counted)
	eor #$FF
	and own,x
	jsr Bits
	inx
	cpx #32
	bne @o
	lda PK_N
	clc
	adc cnt
	ldy #R_BANKS
	sta (kPtr2),y
	ldx #0                  ; all: the own and the chain
@a:	txa
	clc
	adc #R_OWN
	tay
	lda (kPtr2),y
	pha
	tya
	clc
	adc #32
	tay
	pla
	sta (kPtr2),y
	inx
	cpx #32
	bne @a
	ldx #0
@ch:	cpx PK_N
	beq @chd
	phx
	lda PK_LIST,x
	tax
	jsr BankBit
	sta tmp
	tya
	clc
	adc #R_ALL
	tay
	lda (kPtr2),y
	ora tmp
	sta (kPtr2),y
	plx
	inx
	bra @ch
@chd:	jsr TbAll               ; tb: every task's banks
	; the record file is the task's now: bank F forgets it (its banks
	; and state stay with the task), so the next program does not
	; write it back or free it
	LoadW r0, Zero
	LoadW r1, F_RecVars     ; (isOpen)
	LoadW r2, 1
	jsr ToF
	ldx #<MsgAside
	ldy #>MsgAside
	jsr Log
	FARCALL k_EnterDeskTop  ; (does not return)

; the bits of a in cnt (x kept)
Bits:	phx
	ldx #8
@1:	asl
	bcc @2
	inc cnt
@2:	dex
	bne @1
	plx
	rts

; tb = the banks of every task put aside (kPtr2, sl kept)
TbAll:	MoveW kPtr2, keepP
	MoveB sl, keepP+2
	ldx #31
@z:	stz tb,x
	dex
	bpl @z
	ldx #SLOTS - 1
@t:	phx
	jsr Slot
	lda (kPtr2)
	beq @n
	ldy #R_ALL
	ldx #0
@b:	lda (kPtr2),y
	ora tb,x
	sta tb,x
	iny
	inx
	cpx #32
	bne @b
@n:	plx
	dex
	bpl @t
	MoveW keepP, kPtr2
	MoveB keepP+2, sl
	rts

;---------------------------------------------------------------
; Keep: the running application into banks; c set if they ran out
; (nothing changed then)
;---------------------------------------------------------------
Keep:	jsr CtxOut
	jsr IrqOff              ; (V91) its interrupt routines stop: what they
				; would change while it is packed would be lost
				; (zero page, from CtxOut's copy) or half kept
				; (RAM); their vectors are in cVars
	; its own banks: what it uses now that no task holds and is not a
	; spare font (fonts nobody asked for since it started)
	ldx #31
@o:	lda tb,x
	ora spare,x
	eor #$FF
	and used,x
	sta own,x
	dex
	bpl @o
	jsr Tidy
	lda #RESERVE            ; the context bank
	jsr TakeBank
	cpx #0
	bne @no
	pha
	LoadW r0, PkImgA
	LoadW r2, PkLenA
	jsr Image
	pla
	sta PK_LIST
	LoadB PK_N, 1
	LoadW r4, STREAM
	jsr PK_RUN
	bcc @packed
	ldx PK_N                 ; out of banks: the chain freed, the
@fr:	dex                     ; variables the image used given back
	bmi @fr1
	phx
	lda PK_LIST,x
	jsr Unuse
	plx
	bra @fr
@fr1:	jsr LowBack
@no:	jsr VecBack             ; the interrupt's vectors back (V91)
	sec
	rts
@packed:
	; the context bank's head, the context, bank F's part
	lda PK_LIST
	sta r3H
	stz r3L                 ; (below $A000: no bank)
	LoadW r0, PK_N
	LoadW r1, CH_N
	LoadW r2, 1 + CHAIN_MAX
	jsr k_BankCopy
	LoadW r0, Magic
	LoadW r1, $A000
	LoadW r2, 4
	jsr FromW
	LoadW r0, Ctx
	LoadW r1, CTX
	LoadW r2, CTX_LEN
	jsr FromW
	lda kBankBase
	clc
	adc #BANK_F
	sta r3L
	LoadW r0, F_RecVars
	LoadW r1, FREC
	LoadW r2, REC_LEN
	jsr k_BankCopy
	LoadW r0, F_LdVars
	LoadW r1, FLD
	LoadW r2, LD_LEN
	jsr k_BankCopy
	clc
	rts

; the interrupt's vectors back from the context (V91: packing gave up)
VecBack:
	php
	sei
	ldx #VecN - 1
@vb:	ldy Vec,x
	lda cVars + $100,y
	sta $0500,y
	dex
	bpl @vb
	lda rateKeep
	sta AUDIO_RATE
	lda ienKeep
	sta VERA_IEN
	plp
	rts

.segment "BSS"
ienKeep: .res 1
rateKeep: .res 1
.segment "CODE"
Vec:	.byte <intFirstVector, <(intFirstVector + 1), <intTopVector, <(intTopVector + 1)
	.byte <intBotVector, <(intBotVector + 1), <mouseSource, <(mouseSource + 1)
VecN = * - Vec
.assert >intFirstVector = $05 && >intTopVector = $05 && >intBotVector = $05 && >mouseSource = $05, error, "task.s: the vectors in page 5"

; the interrupt calls nothing of the program's (each vector whole:
; interrupts off, V68), and VERA's line interrupt and AFLOW are off, the
; DAC held (V91: nobody would acknowledge them; a program that used them
; arms them again; V52's AudioOff); IEN and the rate kept for VecBack
IrqOff:	php
	sei
	lda VERA_IEN
	sta ienKeep
	lda AUDIO_RATE
	sta rateKeep
.ifndef NO_AUDIO_GUARD          ; (gate audio's control kernel)
	stz AUDIO_RATE
.endif
	stz intFirstVector+1
	stz intFirstVector
	stz intTopVector
	stz intTopVector+1
	stz intBotVector
	stz intBotVector+1
	stz mouseSource
	stz mouseSource+1
	lda VERA_IEN
.ifndef NO_AUDIO_GUARD
	and #<~(VERA_LINEIRQ | VERA_AFLOW)
.else
	and #<~VERA_LINEIRQ
.endif
	sta VERA_IEN
	plp
	rts

; VRAM that holds nothing zeroed, so that it packs to nothing: the free
; part of the save-under stack (bank U's UnderFree up), and the colour
; map's area while the map is off (it is cleared when it is turned on)
Tidy:	LoadW r0, U_UnderFree
	LoadW r1, tmp
	LoadW r2, 3
	lda kBankBase
	clc
	adc #BANK_U
	sta r3L
	adc #BANK_W - BANK_U
	sta r3H
	jsr k_BankCopy
	stz VERA_CTRL
	lda tmp+2               ; (in $12C00-$16BFF)
	cmp #1
	bne @map
	lda #<$6C00
	sec
	sbc tmp
	sta r0L
	lda #>$6C00
	sbc tmp+1
	sta r0H
	bcc @map
	cmp #$40 + 1
	bcs @map
	MoveB tmp, VERA_ADDR_L
	MoveB tmp+1, VERA_ADDR_M
	LoadB VERA_ADDR_H, 1 | VERA_INC1
	jsr Zeros
@map:	bit cCol
	bmi TidyR
	LoadB VERA_ADDR_L, <VRAM_FREE
	LoadB VERA_ADDR_M, >VRAM_FREE
	LoadB VERA_ADDR_H, ^VRAM_FREE | VERA_INC1
	LoadW r0, $1F000 - VRAM_FREE
Zeros:	lda r0L
	ora r0H
	beq @r
@z:	stz VERA_DATA0
	lda r0L
	bne @1
	dec r0H
@1:	dec r0L
	lda r0L
	ora r0H
	bne @z
@r:
TidyR:	rts

; bank a used no more
Unuse:	tax
	jsr BankBit
	eor #$FF
	and used,y
	sta used,y
	rts

; r0 (bank W) -> r1 in bank r3H
FromW:	lda kBankBase
	clc
	adc #BANK_W
	sta r3L
	jmp k_BankCopy

; r0 in bank F -> r1 (bank W), r2 bytes; ToF: r0 (bank W) -> r1 in bank F
FromF:	lda kBankBase
	clc
	adc #BANK_F
	sta r3L
	adc #BANK_W - BANK_F
	sta r3H
	jmp k_BankCopy
ToF:	lda kBankBase
	clc
	adc #BANK_W
	sta r3L
	sbc #BANK_W - BANK_F - 1  ; (c clear: one less)
	sta r3H
	jmp k_BankCopy

; the same with bank S
FromS:	lda kBankBase
	clc
	adc #BANK_S
	sta r3L
	adc #BANK_W - BANK_S
	sta r3H
	jmp k_BankCopy
ToS:	lda kBankBase
	clc
	adc #BANK_W
	sta r3L
	sbc #BANK_W - BANK_S - 1
	sta r3H
	jmp k_BankCopy

; a pack image (r0, r2 bytes) to PK_RUN
Image:	LoadW r1, PK_RUN
	stz r3H
	jmp FromW               ; (r3L = bank W)

; $0400-$04FF and $0600-$07FF back from the context (the image's)
LowBack:
	ldx #0
@1:	lda cVars,x
	sta $0400,x
	lda cVars + $200,x
	sta $0600,x
	lda cVars + $300,x
	sta $0700,x
	inx
	bne @1
	rts

;---------------------------------------------------------------
; CtxOut: the context into Ctx
;---------------------------------------------------------------
CtxOut:	ldx #ZP1 - 1
@z1:	lda $22,x
	sta cZp,x
	dex
	bpl @z1
	ldx #ZP2 - 1
@z2:	lda $50,x
	sta cZp + ZP1,x
	dex
	bpl @z2
	ldx #ZP3 - 1
@z3:	lda $A9,x
	sta cZp + ZP1 + ZP2,x
	dex
	bpl @z3
	ldx #0
@v:	lda $0400,x
	sta cVars,x
	lda $0500,x
	sta cVars + $100,x
	lda $0600,x
	sta cVars + $200,x
	lda $0700,x
	sta cVars + $300,x
	inx
	bne @v
	ldx #SLEEPBUF_SIZE - 1
@sl:	lda SleepN,x
	sta cSleep,x
	dex
	bpl @sl
	ldx #7
@rc:	lda kMenuRect,x
	sta cRect,x
	dex
	bpl @rc
	ldx #FONT_STATE - 1
@fo:	lda sysFont,x
	sta cFont,x
	dex
	bpl @fo
	MoveB uiTimer, cUi
	MoveB uiWake, cUi+1
	ldx #16
@nm:	lda kAppName,x
	sta cName,x
	dex
	bpl @nm
	MoveB curFlags, cFlags
	ldx #31
@b1:	lda used,x
	sta cUsed,x
	dex
	bpl @b1
	ldx #23
@b2:	lda runF,x
	sta cRunF,x
	lda runN,x
	sta cRunN,x
	dex
	bpl @b2
	LoadW r1, cProc
	FARCALL ProcOut
	LoadW r0, S_kColorOn
	LoadW r1, cCol
	LoadW r2, 2
	jsr FromS
	stz VERA_CTRL
	MoveB VERA_DC_VIDEO, cVid
	ldx #13
@l:	lda VERA_L0_CONFIG,x
	sta cLay,x
	dex
	bpl @l
	jsr SprAt
	ldx #0
@s:	lda VERA_DATA0
	sta cSpr,x
	inx
	cpx #64
	bne @s
	jsr PalAt
	ldx #0
@p:	lda VERA_DATA0
	sta cPal,x
	inx
	cpx #32
	bne @p
	rts

SprAt:	LoadB VERA_ADDR_L, <VRAM_SPRATTR
	LoadB VERA_ADDR_M, >VRAM_SPRATTR
	LoadB VERA_ADDR_H, ^VRAM_SPRATTR | VERA_INC1
	rts
PalAt:	LoadB VERA_ADDR_L, <VRAM_PALETTE
	LoadB VERA_ADDR_M, >VRAM_PALETTE
	LoadB VERA_ADDR_H, ^VRAM_PALETTE | VERA_INC1
	rts

;---------------------------------------------------------------
; TASK_END: task x dropped
;---------------------------------------------------------------
End:	jsr Used
	bne @r
	lda #0
	sta (kPtr2)
	jsr Drop
	ldx #0
@r:	rts

; the banks of task sl (its record kPtr2, its state cleared) that nobody
; else holds and that are not fonts are free again; tb without them
Drop:	jsr TbAll
	ldy #R_ALL
	ldx #0
@1:	lda tb,x
	ora keep,x
	eor #$FF
	and (kPtr2),y
	eor #$FF
	and used,x
	sta used,x
	iny
	inx
	cpx #32
	bne @1
	rts

;---------------------------------------------------------------
; TASK_RESUME: task x back; the caller ends
;---------------------------------------------------------------
Resume:	jsr Used
	beq @1
	rts
@1:	jsr Clock0	; the caller ends, as LdApplic ends a program (V76: the interrupt
	; calls nothing of it from now on)
	jsr IrqOff
	FARCALL AudioReset
	FARCALL PrReset
	FARCALL k_CloseRecordFile
	lda #0                  ; its streams and the listing
	FARCALL k_FClose
	ldx #31                 ; its banks: only the fonts' and the tasks'
@b:	lda keep,x              ; stay
	ora tb,x
	sta used,x
	dex
	bpl @b
	ldx #23
@r:	stz runF,x
	dex
	bpl @r
	; its video mode (the screens cleared: they come back below)
	ldx sl
	jsr Slot
	ldy #R_CTX
	lda (kPtr2),y
	sta r3L
	sta tmp
	jsr ToW0
	LoadW r0, CTX + (cVars - Ctx) + (graphMode - $0400)
	LoadW r1, tmp+1
	LoadW r2, 1
	jsr k_BankCopy
	lda tmp+1
	FARCALL k_SetVideoMode
	; the chain's list for the image, and the image
	LoadW r0, PkImgB
	LoadW r2, PkLenB
	jsr Image
	lda tmp
	sta r3L
	stz r3H
	LoadW r0, CH_N
	LoadW r1, PK_N
	LoadW r2, 1 + CHAIN_MAX
	jsr k_BankCopy
	LoadW r4, STREAM
	jsr PK_RUN
	; the context, bank F's part
	jsr ToW0
	LoadW r0, CTX
	LoadW r1, Ctx
	LoadW r2, CTX_LEN
	jsr k_BankCopy
	lda kBankBase
	clc
	adc #BANK_F
	sta r3H
	LoadW r0, FREC
	LoadW r1, F_RecVars
	LoadW r2, REC_LEN
	jsr k_BankCopy
	LoadW r0, FLD
	LoadW r1, F_LdVars
	LoadW r2, LD_LEN
	jsr k_BankCopy
	LoadW r0, cProc
	FARCALL ProcIn
	LoadW r0, cCol
	LoadW r1, S_kColorOn
	LoadW r2, 2
	jsr ToS
	MoveB cFlags, curFlags  ; it runs: not the desktop (bank S's isDesk too)
	stz curDesk
	LoadW r0, Zero
	LoadW r1, S_isDesk
	LoadW r2, 1
	jsr ToS
	; the bank table: its own, the fonts', the other tasks'
	ldx sl
	jsr Slot
	ldy #R_NOTE             ; (its note again)
	ldx #0
@nt:	lda (kPtr2),y
	sta curNote,x
	iny
	inx
	cpx #TASK_NOTE_LEN
	bne @nt
	lda #0
	sta (kPtr2)
	jsr TbAll
	ldy #R_OWN
	ldx #0
@u:	lda (kPtr2),y
	ora keep,x
	ora tb,x
	sta used,x
	iny
	inx
	cpx #32
	bne @u
	ldx #23
@ru:	lda cRunF,x
	sta runF,x
	lda cRunN,x
	sta runN,x
	dex
	bpl @ru
	; the rest with the interrupt off
	sei
	jsr CtxIn
	cli
	lda #0                  ; its banked font: the index table again
	FARCALL FontSel
	LoadB kSndReq, SND_APP
	lda curDirName          ; its working folder
	beq @go
	LoadW r0, curDirName
	FARCALL k_ChDir
@go:	ldx #<MsgBack
	ldy #>MsgBack
	jsr Log
	ldx #$FF                ; the top of its main loop, an empty stack, its
	txs                     ; bank mapped (CallRoutine: resident)
	lda #<k_MainLoop
	ldx #>k_MainLoop
	jmp k_CallRoutine

; r3L = the context bank (tmp), r3H = bank W
ToW0:	lda tmp
	sta r3L
	lda kBankBase
	clc
	adc #BANK_W
	sta r3H
	rts

;---------------------------------------------------------------
; CtxIn: the context back from Ctx (interrupts off)
;---------------------------------------------------------------
CtxIn:	stz VERA_CTRL
	ldx #13
@l:	lda cLay,x
	sta VERA_L0_CONFIG,x
	dex
	bpl @l
	lda VERA_DC_VIDEO
	and #%10001111
	sta kTmp
	lda cVid
	and #%01110000          ; the layers and sprites as they were
	ora kTmp
	sta VERA_DC_VIDEO
	jsr SprAt
	ldx #0
@s:	lda cSpr,x
	sta VERA_DATA0
	inx
	cpx #64
	bne @s
	jsr PalAt
	ldx #0
@p:	lda cPal,x
	sta VERA_DATA0
	inx
	cpx #32
	bne @p
	ldx #SLEEPBUF_SIZE - 1
@sl:	lda cSleep,x
	sta SleepN,x
	dex
	bpl @sl
	ldx #7
@rc:	lda cRect,x
	sta kMenuRect,x
	dex
	bpl @rc
	ldx #FONT_STATE - 1
@fo:	lda cFont,x
	sta sysFont,x
	dex
	bpl @fo
	MoveB cUi, uiTimer
	MoveB cUi+1, uiWake
	ldx #16
@nm:	lda cName,x
	sta kAppName,x
	dex
	bpl @nm
	; the system variables, but what is live stays
	ldx #LiveN - 1
@l1:	ldy Live,x
	lda $0500,y
	sta live,x
	dex
	bpl @l1
	ldx #0
@v:	lda cVars,x
	sta $0400,x
	lda cVars + $100,x
	sta $0500,x
	lda cVars + $200,x
	sta $0600,x
	lda cVars + $300,x
	sta $0700,x
	inx
	bne @v
	ldx #LiveN - 1
@l2:	ldy Live,x
	lda live,x
	sta $0500,y
	dex
	bpl @l2
	; zero page (not pressFlag and the pointer: $39-$3C)
	ldx #ZP1 - 1
@z1:	cpx #$39 - $22
	bcc @z1a
	cpx #$3D - $22
	bcc @z1b
@z1a:	lda cZp,x
	sta $22,x
@z1b:	dex
	bpl @z1
	ldx #ZP2 - 1
@z2:	lda cZp + ZP1,x
	sta $50,x
	dex
	bpl @z2
	ldx #ZP3 - 1
@z3:	lda cZp + ZP1 + ZP2,x
	sta $A9,x
	dex
	bpl @z3
	rts

; the live system variables (offsets from $0500), as kernel/s/desk.s's
Live:	.byte <mouseData, <random, <(random + 1), <dblClickCount, <year, <month, <day
	.byte <hour, <minutes, <seconds, <mouseYPosW, <(mouseYPosW + 1), <mouseButtons
	.byte <mouseWheel, <keyMods, <blankDelay, <(blankDelay + 1)
LiveN = * - Live

;---------------------------------------------------------------
; Ask (Poll: why 1 busy, 0 not enough memory): a box over the program,
; "<name> / cannot be put aside now. / Quit it, losing its work?", OK
; and Cancel; OK: it ends (EnterDeskTop), Cancel: it goes on
;---------------------------------------------------------------
DLG     = dbFileNames
L1      = DLG + 32
L2      = DLG + 64
L3      = DLG + 96
Ask:	ldx #127
@k:	lda DLG,x
	sta dlgKeep,x
	dex
	bpl @k
	ldx #BoxEnd - Box - 1
@b:	lda Box,x
	sta DLG,x
	dex
	bpl @b
	lda #BOLDON
	sta L1
	ldx #0
@n:	lda kAppName,x
	sta L1 + 1,x
	beq @n1
	inx
	cpx #16
	bne @n
	stz L1 + 1,x
@n1:	ldx #31
@t:	lda TxtMem,x
	ldy why
	beq @t1
	lda TxtBusy,x
@t1:	sta L2,x
	lda TxtQuit,x
	sta L3,x
	dex
	bpl @t
	ldx #<MsgAskM
	ldy #>MsgAskM
	lda why
	beq @lg
	ldx #<MsgAskB
	ldy #>MsgAskB
@lg:	jsr Log
.ifdef SWDEBUG
	lda #'['
	sta EMU_STDOUT
	ldx #0
@dd:	lda tmp,x
	ora #'0'
	sta EMU_STDOUT
	inx
	cpx #4
	bne @dd
.endif
	LoadW r0, DLG
	FARCALL k_DoDlgBox
	ldx #127
@r:	lda dlgKeep,x
	sta DLG,x
	dex
	bpl @r
	lda sysDBData
	cmp #OK
	beq @quit
	rts
@quit:	ldx #<MsgQuit
	ldy #>MsgQuit
	jsr Log
	FARCALL k_EnterDeskTop  ; (does not return)

; the console (x16emu only): "TASK <x/y> <name>"
Log:	jsr IsEmu
	bne @r
	phy
	phx
	ldx #<MsgTask
	ldy #>MsgTask
	jsr LogStr
	plx
	ply
	jsr LogStr
	ldx #<kAppName
	ldy #>kAppName
	jsr LogStr
	lda #' '                ; the cycles since Clock0 (hex)
	sta EMU_STDOUT
	lda EMU_CLOCK           ; (latches)
	sta t0
	ldx #1
@c:	lda EMU_CLOCK,x
	sta t0,x
	inx
	cpx #4
	bne @c
	ldx #3
@h:	lda t0,x
	jsr Hex
	dex
	bpl @h
	jmp LogNL
@r:	rts
Hex:	pha
	lsr
	lsr
	lsr
	lsr
	jsr @d
	pla
	and #$0F
@d:	cmp #10
	bcc @1
	adc #'A' - '0' - 10 - 1
@1:	adc #'0'
	sta EMU_STDOUT
	rts
; the emulator's cycle counter from 0 (x16emu only: a write resets it)
Clock0:	jsr IsEmu
	bne @r
	sta EMU_CLOCK
@r:	rts

.segment "RODATA"

Zero:	.byte 0
Magic:	.byte "VTSK"
Box:	.byte DEF_DB_POS | 1
	.byte DBTXTSTR, TXT_LN_X, TXT_LN_1_Y
	.word L1
	.byte DBTXTSTR, TXT_LN_X, TXT_LN_2_Y
	.word L2
	.byte DBTXTSTR, TXT_LN_X, TXT_LN_3_Y
	.word L3
	.byte OK, DBI_X_0, DBI_Y_2
	.byte CANCEL, DBI_X_2, DBI_Y_2
	.byte 0
BoxEnd:
.assert BoxEnd - Box <= 32, error, "the question's box"
TxtBusy: .byte "cannot be put aside now.", 0
	.res 32 - 25
TxtMem:	.byte "No memory to put it aside.", 0
	.res 32 - 27
TxtQuit: .byte "Quit it, losing its work?", 0
	.res 32 - 26
MsgTask: .byte 10, "TASK ", 0
MsgAside: .byte "ASIDE ", 0
MsgBack: .byte "BACK ", 0
MsgAskM: .byte "ASK MEMORY ", 0
MsgAskB: .byte "ASK BUSY ", 0
MsgQuit: .byte "QUIT ", 0
