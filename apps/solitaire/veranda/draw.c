/*
 * Veranda Solitaire: drawing the table. Only what changed is drawn again:
 * the engine reports each stack's lowest changed card (sol_changed); that
 * card's place down to the stack's old or new end is cleared to the
 * table and the cards that reach into it are drawn again, clipped to it
 * (a covered card only down to where the next one covers it, the rounded
 * corners merged by blit.s). The pictures come from RAM banks.
 *
 * Veranda Look 2 (Veranda's DECISIONS V74, V77): the table is felt, the
 * colour map's cards of HILL green under the table's pattern, except the
 * cards a card picture reaches into, which keep the normal paper: the
 * cards stay black and white. The status bar is chrome. None of it with
 * the Classic theme (no colour map).
 *
 * GPL-3.0-or-later (see COPYING).
 */
#include <string.h>
#include "ui.h"
#include "cards.h"

u8 card_bank[8];
u16 secs;
u8 look;                                /* the colours (ui.c: not Classic) */

static u8 dirty[MAXSTACKS];             /* the lowest changed card; ANY clean */
static u8 d_od[MAXSTACKS], d_ou[MAXSTACKS];
static u16 d_x1[MAXSTACKS], d_y1[MAXSTACKS];    /* what was drawn: right, bottom */
static u8 d_n[MAXSTACKS];                       /* and how many cards */

/* the clip: the area being drawn again */
static u16 cx0, cy0, cx1, cy1;

void sol_changed(u8 s, u8 from)
{
    if (from < dirty[s])
        dirty[s] = from;
    /* the talon's empty place shows whether it can turn the waste over */
    if (s == st_waste && !s_n[st_talon])
        dirty[st_talon] = 0;
}

void table_fill(u16 x0, u16 y0, u16 x1, u16 y1)
{
    if (y0 < PLAY_TOP)
        y0 = PLAY_TOP;
    if (y1 > PLAY_BOTTOM)
        y1 = PLAY_BOTTOM;
    if (x1 > 640)
        x1 = 640;
    if (y1 <= y0 || x1 <= x0)
        return;
    bl_x = (u8)(x0 >> 3);
    bl_w = (u8)((x1 >> 3) - bl_x);
    bl_y = y0;
    bl_h = y1 - y0;
    bl_fill();
}

/* picture pic at x, y: its rows [0, vis) and columns [0, cols), clipped */
static void pic_at(u8 pic, u16 x, u16 y, u8 vis, u8 cols)
{
    u16 ra, rb;
    u8 k;
    if (y + vis <= cy0 || y >= cy1 || x + cols * 8 <= cx0 || x >= cx1)
        return;
    ra = cy0 > y ? cy0 - y : 0;
    rb = cy1 - y;
    if (rb > vis)
        rb = vis;
    if (ra >= rb)
        return;
    bl_r0 = (u8)ra;
    bl_r1 = (u8)rb;
    bl_c0 = cx0 > x ? (u8)((cx0 - x) >> 3) : 0;
    bl_c1 = (u8)((cx1 - x) >> 3);
    if (bl_c1 > cols)
        bl_c1 = cols;
    if (bl_c0 >= bl_c1)
        return;
    k = pic / PIC_PER_RECORD;
    bl_bank = card_bank[k];
    bl_src = 0xA000 + (u16)(pic - k * PIC_PER_RECORD) * PIC_BYTES;
    bl_x = (u8)(x >> 3);
    bl_y = y;
    bl_card();
}

void draw_pic(u8 pic, u16 x, u16 y, u8 vis)
{
    cx0 = 0;
    cx1 = 640;
    cy0 = PLAY_TOP;
    cy1 = PLAY_BOTTOM;
    pic_at(pic, x, y, vis, CARD_B);
}

static u8 picture(u8 c)
{
    return UP(c) ? CARDID(c) : PIC_BACK;
}

/* an empty stack's place */
static u8 place(u8 s)
{
    switch (s_kind[s]) {
    case K_FOUND:
        return PIC_FOUND + s_suit[s];
    case K_SPIDERF:
        return PIC_KING;
    case K_PYRROW:
        return ANY;
    case K_TALON:
        return sol_can_deal() ? PIC_REDEAL : PIC_EMPTY;
    }
    return PIC_EMPTY;
}

/* stack s's cards (or its place) into the clip */
static void draw_stack(u8 s)
{
    u8 i, n = s_n[s], od, ou, vis, p;
    u16 x, y, nx, ny;
    if (l_fan[s] == FAN_HIDDEN)
        return;
    if (!n) {
        p = place(s);
        if (p != ANY)
            pic_at(p, l_x[s], l_y[s], CARD_H, CARD_B);
        return;
    }
    switch (l_fan[s]) {
    case FAN_NONE:
        pic_at(picture(s_c[s][n - 1]), l_x[s], l_y[s], CARD_H, CARD_B);
        break;
    case FAN_RIGHT:
        i = n > l_show[s] ? n - l_show[s] : 0;
        for (; i < n; ++i) {
            card_xy(s, i, &x, &y);
            pic_at(picture(s_c[s][i]), x, y, CARD_H, i + 1 < n ? FAN_DX / 8 + 1 : CARD_B);
        }
        break;
    case FAN_DOWN:
        fan_offsets(s, &od, &ou);
        x = l_x[s];
        y = l_y[s];
        for (i = 0; i < n; ++i) {
            vis = CARD_H;
            ny = y + (UP(s_c[s][i]) ? ou : od);
            if (i + 1 < n)
                vis = (u8)(ny - y + CORNER_ROWS);
            if (y + vis > cy0 && y < cy1)
                pic_at(picture(s_c[s][i]), x, y, vis, CARD_B);
            y = ny;
        }
        break;
    }
    (void)nx;
}

/* the colour map's cards of the area [x0, x1) x [y0, y1), whole cards:
   the felt (FELT), but the normal paper on every card a stack's pictures
   reach into (the stack's rectangle as remember() keeps it: the cards are
   8-pixel aligned across, and their rows start on cards, layout.h). A
   stack not yet remembered in this pass of draw_dirty has its old
   rectangle: the cards where old and new differ are in its own area,
   coloured again when it comes. ColorRect (COLOR_BUF) takes FELT_ROWS
   rows at a time */
#define FELT_ROWS 8
static u8 cbuf[FELT_ROWS * 80];
static u8 hx0[MAXSTACKS], hxn[MAXSTACKS];       /* (from c0; cards) */
static u16 hy0[MAXSTACKS], hy1[MAXSTACKS];

static void felt(u16 x0, u16 y0, u16 x1, u16 y1)
{
    u8 c0, c1, w, ry, ry1, s, n, k, a, b, i;
    u16 top, sx0, sy0, sx1, sy1;
    u8 *p, *q;
    if (!look)
        return;
    c0 = (u8)(x0 >> 3);
    c1 = (u8)((x1 + 7) >> 3);
    if (c1 > 80)
        c1 = 80;
    ry = (u8)(y0 >> 3);
    ry1 = (u8)((y1 + 7) >> 3);
    if (ry < FELT_ROW0)
        ry = FELT_ROW0;
    if (ry1 > FELT_ROW1)
        ry1 = FELT_ROW1;
    if (c0 >= c1 || ry >= ry1)
        return;
    w = c1 - c0;
    n = 0;
    for (s = 0; s < nstacks; ++s) {
        if (l_fan[s] == FAN_HIDDEN || (!s_n[s] && place(s) == ANY))
            continue;
        sx0 = l_x[s];
        sy0 = l_y[s];
        sx1 = d_x1[s];
        sy1 = d_y1[s];
        if (sx1 <= (u16)c0 << 3 || sx0 >= (u16)c1 << 3 || sy1 <= (u16)ry << 3 || sy0 >= (u16)ry1 << 3)
            continue;
        a = (u8)(sx0 >> 3);
        a = a > c0 ? a - c0 : 0;
        b = (u8)((sx1 + 7) >> 3);
        b = (b < c1 ? b : c1) - c0;
        hx0[n] = a;
        hxn[n] = b - a;
        hy0[n] = sy0;
        hy1[n] = sy1;
        ++n;
    }
    while (ry < ry1) {
        i = ry1 - ry > FELT_ROWS ? FELT_ROWS : ry1 - ry;
        memset(cbuf, FELT, (u16)i * w);
        for (k = 0, p = cbuf; k < i; ++k, p += w) {
            top = (u16)(ry + k) << 3;
            for (a = 0; a < n; ++a) {
                if (hy0[a] >= top + 8 || hy1[a] <= top)
                    continue;
                q = p + hx0[a];         /* (cc65 2.19 -O made "p + hx0[a]" as an argument hx0[0]) */
                memset(q, 0, hxn[a]);
            }
        }
        r0 = (u16)cbuf;
        ColorRect(c0, w, ry, i, COLOR_BUF);
        ry += i;
    }
}

/* the felt off (the win's cards fly over the whole table) */
void felt_off(void)
{
    if (look)
        ColorRect(0, 80, FELT_ROW0, FELT_ROW1 - FELT_ROW0, 0);
}

/* the area [x0, x1) x [y0, y1) again: the table, then every stack in it */
static void redraw_area(u16 x0, u16 y0, u16 x1, u16 y1)
{
    u8 s;
    u16 a, b, c, d;
    if (y0 < PLAY_TOP)
        y0 = PLAY_TOP;
    if (y1 > PLAY_BOTTOM)
        y1 = PLAY_BOTTOM;
    if (y1 <= y0)
        return;
    table_fill(x0, y0, x1, y1);
    cx0 = x0;
    cy0 = y0;
    cx1 = x1;
    cy1 = y1;
    for (s = 0; s < nstacks; ++s) {
        if (l_fan[s] == FAN_HIDDEN)
            continue;
        stack_rect(s, &a, &b, &c, &d);
        if (c <= x0 || a >= x1 || d <= y0 || b >= y1)
            continue;
        draw_stack(s);
    }
    felt(x0, y0, x1, y1);
}

static void remember(u8 s)
{
    u16 a, b;
    stack_rect(s, &a, &b, &d_x1[s], &d_y1[s]);
    d_n[s] = s_n[s];
    if (l_fan[s] == FAN_DOWN)
        fan_offsets(s, &d_od[s], &d_ou[s]);
    dirty[s] = ANY;
}

void draw_all(void)
{
    u8 s;
    static const u8 pat[4] = { 0x88, 0x00, 0x22, 0x00 };
    memcpy(bl_pat, pat, 4);
    layout_init();
    for (s = 0; s < nstacks; ++s)
        remember(s);
    redraw_area(0, PLAY_TOP, 640, PLAY_BOTTOM);
}

void draw_dirty(void)
{
    u8 s, od, ou, from;
    u16 x0, y0, x1, y1, x, y;
    for (s = 0; s < nstacks; ++s) {
        from = dirty[s];
        if (from == ANY)
            continue;
        if (l_fan[s] == FAN_HIDDEN) {
            dirty[s] = ANY;
            continue;
        }
        stack_rect(s, &x0, &y0, &x1, &y1);
        if (d_x1[s] > x1)
            x1 = d_x1[s];
        if (d_y1[s] > y1)
            y1 = d_y1[s];
        if (l_fan[s] == FAN_DOWN) {
            fan_offsets(s, &od, &ou);
            if (od != d_od[s] || ou != d_ou[s])
                from = 0;
            if (from && from <= s_n[s]) {
                card_xy(s, from - 1, &x, &y);
                y += UP(s_c[s][from - 1]) ? ou : od;
                y0 = y;
            }
        } else if (l_fan[s] == FAN_RIGHT && from && s_n[s] <= l_show[s] && d_n[s] <= l_show[s]
                   && from <= s_n[s]) {
            /* the cards before from stay; the area from card from - 1's right
               side. Only if no card moved over: with more than l_show cards
               (before or after) the shown ones start at the base, so a card
               taken off a waste of l_show + 1 lay over the others (Klondike's
               waste of 2 going to 1: its corner stayed on the screen, the
               card under it not drawn, as on the user's R49) */
            card_xy(s, from - 1, &x, &y);
            x0 = x + FAN_DX;
        }
        remember(s);
        redraw_area(x0, y0, x1, y1);
    }
}

/* a 1-pixel frame inverted, clipped to the table (the dragged outline) */
void xor_frame(int x0, int y0, int x1, int y1)
{
    int a, b;
    if (x0 < 0) x0 = 0;
    if (x1 > 639) x1 = 639;
    if (x1 < x0)
        return;
    a = y0 < PLAY_TOP ? PLAY_TOP : y0;
    b = y1 >= PLAY_BOTTOM ? PLAY_BOTTOM - 1 : y1;
    if (b < a)
        return;
    bl_x0 = (u16)x0;
    bl_x1 = (u16)x1;
    if (y0 >= PLAY_TOP && y0 < PLAY_BOTTOM) {
        bl_y = (u16)y0;
        bl_xorh();
    }
    if (y1 >= PLAY_TOP && y1 < PLAY_BOTTOM && y1 != y0) {
        bl_y = (u16)y1;
        bl_xorh();
    }
    /* the sides, between the top and bottom rows */
    if (y0 >= PLAY_TOP)
        ++a;
    if (y1 < PLAY_BOTTOM)
        --b;
    while (a <= b) {
        int n = b - a + 1;
        if (n > 255)
            n = 255;
        bl_y = (u16)a;
        bl_h = (u16)n;
        bl_x0 = (u16)x0;
        bl_xorv();
        if (x1 != x0) {
            bl_y = (u16)a;
            bl_h = (u16)n;
            bl_x0 = (u16)x1;
            bl_xorv();
        }
        a += n;
    }
}

/*---------------------------------------------------------------
  the status line: the game and deal, moves, time, cards left
 ---------------------------------------------------------------*/
static char sb[48];

static char *dec(char *p, u32 v)
{
    char d[11];
    u8 n = 0;
    do {
        d[n++] = (char)('0' + (u8)(v % 10));
        v /= 10;
    } while (v);
    while (n)
        *p++ = d[--n];
    *p = 0;
    return p;
}

/* the four fields; each drawn again only when its text changes */
static char shown[4][32];

static void field(u8 k, u16 x0, u16 x1, const char *t)
{
    if (!strcmp(shown[k], t))
        return;
    strcpy(shown[k], t);
    SetPattern(0);
    WRectangle(x0, STATUS_Y + 1, x1, 479);
    WPutString((void *)t, x0 + 6, STATUS_Y + 2 + baselineOffset);
}

void draw_time(void)
{
    char *p;
    strcpy(sb, "Time ");
    p = dec(sb + 5, secs / 60);
    *p++ = ':';
    *p++ = (char)('0' + (secs % 60) / 10);
    *p++ = (char)('0' + secs % 10);
    *p = 0;
    field(2, 380, 479, sb);
}

/* the status line; all of it again after the table was drawn whole */
void draw_status(void)
{
    char *p;
    strcpy(sb, G->name);
    p = sb + strlen(sb);
    strcpy(p, "   #");
    dec(p + 4, deal_no);
    field(0, 0, 279, sb);
    strcpy(sb, "Moves ");
    dec(sb + 6, moves);
    field(1, 280, 379, sb);
    draw_time();
    strcpy(sb, won ? "Won!" : "Cards left ");
    if (!won)
        dec(sb + 11, sol_left());
    field(3, 480, 639, sb);
}

void status_all(void)
{
    memset(shown, 0, sizeof shown);
    if (look)
        ColorRect(0, 80, STATUS_Y >> 3, 60 - (STATUS_Y >> 3), CHROME);     /* (Look 2: the status bar) */
    WHorizontalLine(0xFF, 0, 639, STATUS_Y);
    draw_status();
}
