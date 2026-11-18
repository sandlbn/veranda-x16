; Veranda bank U: menus, icons, dialog boxes, text input (phase 4)
;
; The main loop asks these first; each returns c set if it took the event.

.include "kernel.inc"

.export UiKey

.segment "CODE"

; a key in keyData: GetString's text input (phase 4)
UiKey:	clc
	rts

