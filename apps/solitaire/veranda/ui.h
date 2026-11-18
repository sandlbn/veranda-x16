/*
 * Veranda Solitaire on Veranda: the parts of the program (main.c, draw.c,
 * blit.s, and the test build's hooks). GPL-3.0-or-later (see COPYING).
 */
#ifndef UI_H
#define UI_H
#include <veranda.h>
#include <vr.h>
#include <vrpath.h>      /* the card's folders (Veranda DECISIONS V71) */
#include <theme.h>       /* Veranda Look 2's palette and menu bar (V74, V79) */
#include <vrwin.h>       /* vr_bar_end: the menu bar's end */
#include "../src/sol.h"
#include "../src/layout.h"

#define RAM_BANK (*(volatile u8 *)0)

/* blit.s */
extern u8 bl_bank, bl_x, bl_r0, bl_r1, bl_c0, bl_c1, bl_w;
extern u16 bl_src, bl_y, bl_h, bl_x0, bl_x1;
extern u8 bl_pat[4];
void bl_card(void);
void bl_fill(void);
void bl_xorh(void);
void bl_xorv(void);

/* draw.c */
#define FELT       HILL                 /* the table's colour (theme.h) */
#define FELT_ROW0  (PLAY_TOP >> 3)      /* its card rows: 2 (the bar's line) */
#define FELT_ROW1  ((PLAY_BOTTOM + 7) >> 3)     /* to 57 */
extern u8 look;                         /* Look 2's colours (not Classic) */
void felt_off(void);
extern u8 card_bank[8];                 /* the pictures' banks (records 1-5) */
void draw_all(void);                    /* the whole table */
void draw_dirty(void);                  /* the stacks that changed */
void draw_pic(u8 pic, u16 x, u16 y, u8 vis);    /* a picture, whole, clipped to the table */
void table_fill(u16 x0, u16 y0, u16 x1, u16 y1);
void xor_frame(int x0, int y0, int x1, int y1);
void draw_status(void);                 /* the fields that changed */
void status_all(void);                  /* the whole status line */
void draw_time(void);
extern u16 secs;

/* main.c */
extern u8 busy;
extern volatile u8 frames;
void ui_main(void);             /* ui.c (overlay 1) */
u8 ovl_bank(u8 n);
void leave(void);               /* to the desktop, the interrupt routine off */

#ifdef SOL_TEST
void sol_test_start(void);
void sol_test_key(void);
void sol_test_slow(void);
extern volatile u8 sol_slow;
void clock_reset(void);
void clock_log(const char *what);
#define CLOCK_RESET() clock_reset()
#define CLOCK_LOG(w) clock_log(w)
#else
#define CLOCK_RESET()
#define CLOCK_LOG(w)
#endif
#endif
