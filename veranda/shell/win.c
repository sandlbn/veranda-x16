/* Veranda shell: the windows and the desktop (overlay 1; Shell 2,
   DECISIONS V70; Veranda Look 2, V77).

   The screen: the menu bar (rows 0-15: the menus at the left, the date
   and time at the right), the desktop under it (a GEOS pattern on the
   theme's desktop colour), its icons in a column at the right (tiles:
   the card, the printer, the trash, then the pinned applications, each
   labelled in white on a dark pill) and up to two folder windows over
   it.

   A window (x and width in cards, y and height in multiples of 8, so its
   bands are whole cards and each takes its colour): its chrome (chrome.c:
   the title band, the toolbar, the sidebar, the status bar, the shadow)
   and its contents (view.c).

   The screen is drawn to both screens; the back screen keeps a copy, so
   menus and dialog boxes recover from it and a window's parts are drawn
   again where they are uncovered: the scene (the desktop, its icons, the
   windows bottom up, with their shadows) is drawn into the back screen
   alone, then copied to the foreground (RecoverRectangle) where it
   changed. Dragging (a window's title band, its resize grip, an icon)
   shows an outline on the foreground only (inverted, so drawn again it
   is gone); a window let go is moved with CopyRect, and what it
   uncovered drawn again. No private copies of windows: VRAM holds the
   two screens.

   Dropping an icon: on the other window, a copy into its folder (Ctrl or
   Shift held: a move); on a folder's icon, a move into it (TopDesk's
   way within a card; Ctrl or Shift: a copy); on the card, a copy into
   its top folder; on the trash, the trash (files.c); on the printer, a
   print (print.c); an application on the desktop: pinned. A pin dropped
   on the trash is unpinned. */
#include <veranda.h>
#include <string.h>
#include <theme.h>
#include "shell.h"
#include "winp.h"
#include "tiles.h"
#pragma code-name ("OVERLAY1")


unsigned int cx0, cy0, cx1, cy1;
unsigned char foundWin;         /* window + 1 that shows the find's results, not in front */

unsigned int SW, SH;
unsigned char deskX;            /* the desktop icons' column (cards) */

/* the desktop's icons */
unsigned char ndesk;
unsigned char dkind[MAXDESK], didx[MAXDESK];
unsigned int dy[MAXDESK];
unsigned char deskSel = 0xFF;
static unsigned char dlc[MAXDESK], dlw[MAXDESK];        /* their labels' cards */
static unsigned char trashIsFull;

static const unsigned char pats[8] = { 2, 3, 5, 31, 23, 19, 0, 33 };


/*---------------------------------------------------------------- the layout */

static void place(unsigned char w, unsigned char x, unsigned char ww, unsigned int y, unsigned int h)
{
    struct win *p = &wins[w];
    p->x = x;
    p->w = ww;
    p->y = y;
    p->h = h;
}

/* a window inside the screen, at least 24 cards by 112 rows */
void win_clamp(unsigned char w)
{
    struct win *p = &wins[w];
    unsigned char cols = SW / 8;
    if (p->w < 24)
        p->w = 24;
    if (p->w > cols)
        p->w = cols;
    if (p->h < 112)
        p->h = 112;
    if (p->h > SH - 16)
        p->h = SH - 16;
    if (p->x + p->w > cols)
        p->x = cols - p->w;
    if (p->y < 16)
        p->y = 16;
    if (p->y + p->h > SH)
        p->y = SH - p->h;
}

/* the desktop's icons: the card, the printer, the trash, the pins; a
   tile (4 card rows) and its label (2) every 7 rows (6 at 240 rows) */
static void deskLayout(void)
{
    unsigned char i, step = SH > 240 ? 56 : 48;
    unsigned int y = SH > 240 ? 32 : 24;
    ndesk = 0;
    dkind[ndesk] = K_CARD;
    dy[ndesk++] = y;
    dkind[ndesk] = K_PRINTER;
    dy[ndesk++] = y += step;
    dkind[ndesk] = K_TRASH;
    dy[ndesk++] = y += step;
    (void)i;                    /* (the pins: in the dock since V83) */
}

/* the layout for the video mode: the windows' default places (or the
   desk file's, kept for this mode: dir.c sets them), clamped */
void win_init(void)
{
    SW = screenWidth;
    SH = screenHeight;
    deskX = SW / 8 - 10;
    if (!wins[0].w || wins[0].h >= 0x8000) {
        /* (above the dock, V83: its top at SH - 64) */
        if (SW > 320)
            place(0, 2, 60, SH > 240 ? 32 : 24, SH > 240 ? SH - 112 : SH - 88);
        else
            place(0, 0, deskX - 1, 24, SH - 88);
    }
    if (!wins[1].w || wins[1].h >= 0x8000) {
        if (SW > 320)
            place(1, 20, 48, SH > 240 ? 120 : 64, SH > 240 ? SH - 200 : SH - 176);
        else
            place(1, 4, 25, 64, SH - 176);
    }
    win_clamp(0);
    win_clamp(1);
    deskLayout();
}

/*---------------------------------------------------------------- text */

static unsigned int width(const char *s)
{
    unsigned int w = 0;
    while (*s)
        w += GetCharWidth(*s++);
    return w;
}

/*---------------------------------------------------------------- colours */

static unsigned char deskTile(unsigned char i)
{
    switch (dkind[i]) {
    case K_CARD:
        return TL_BUILT + T_CARD;
    case K_PRINTER:
        return TL_BUILT + T_PRINTER;
    case K_TRASH:
        return TL_BUILT + T_TRASH;
    }
    return pinTile[didx[i]];
}

/* 1 if cards x, y (w by h) meet the colour clip, or it is off */
static unsigned char colSeen(unsigned char x, unsigned char w, unsigned char y, unsigned char h)
{
    return !colClip || (x <= colX1 && x + w > colX0 && y <= colY1 && y + h > colY0);
}

/* the colour map: the menu bar, the desktop, its icons, the windows
   bottom up (their chrome, their contents); with the colour clip on
   (look.c: colClip) only its cards are written */
void win_colours(void)
{
    unsigned char w, z, i, cw = SW / 8;
    if (wallMode == W_PICTURE)          /* (the picture's cards drawn since: V78) */
        XCALL(bWall, wall_flush, FV)();
    else
        look_rect(0, cw, 2, SH / 8 - 2, theme.desk);
    for (i = 0; i < ndesk; ++i) {
        if (!colSeen(deskX - 1, 11, dy[i] >> 3, 6))    /* (the colour clip: V81) */
            continue;
        look_rect(deskX + 2, 4, dy[i] >> 3, 4, XCALL(bTile, tile_colour, unsigned char (*)(unsigned char, unsigned char))(2, deskTile(i)));
        look_rect(dlc[i], dlw[i], (dy[i] >> 3) + 4, 2, i == deskSel ? (theme.hilite ? theme.hilite : SELECT) : 0);
    }
    for (z = 0; z < 2; ++z) {
        w = z ? front : front ^ 1;
        if (w >= NWIN || !wins[w].open || !colSeen(wins[w].x, wins[w].w, wins[w].y >> 3, wins[w].h >> 3))
            continue;
        XCALL(bBars, chrome_colours, FB)(w);
        if (!(found_on && w == front))
            XCALL(bView, view_colours, FB)(w);
    }
    XCALL(bDock, dock_colours, FV)();
}

/*---------------------------------------------------------------- the desktop */

/* the desktop in l, t, r, b: the picture (whole bytes: the scene widens
   its area to them), the plain colour, or the pattern (DECISIONS V78) */
static void deskFill(unsigned int l, unsigned int t, unsigned int r, unsigned int b)
{
    if (clipOn) {                       /* (the picture is not the kernel's: the clip here) */
        if (l < leftMargin)
            l = leftMargin;
        if (r > rightMargin)
            r = rightMargin;
        if (t < windowTopW)
            t = windowTopW;
        if (b > windowBottomW)
            b = windowBottomW;
        if (l > r || t > b)
            return;
    }
    if (t < 16)
        t = 16;
    if (wallMode == W_PICTURE) {
        XCALL(bWall, wall_fill, void (*)(unsigned int, unsigned int, unsigned int, unsigned int))(l, t, r, b);
        return;
    }
    SetPattern(wallMode == W_PLAIN ? 0 : pats[deskPat & 7]);
    WRectangle(l, t, r, b);
}

static void deskLabel(unsigned char i, char *s)
{
    switch (dkind[i]) {
    case K_CARD:
        strcpy(s, cardName[0] ? cardName : "SD card");
        break;
    case K_PRINTER:
        strcpy(s, "Printer");
        break;
    case K_TRASH:
        strcpy(s, "Trash");
        break;
    default:
        strcpy(s, pins[didx[i]].name);
        if (shell_is_cvt(s))
            s[strlen(s) - 4] = 0;
    }
}

/* desktop icon i: its tile, its label (white on a dark pill), a ring
   round the tile when it is selected; on the desktop's pattern */
void win_deskicon(unsigned char i)
{
    unsigned int x = (deskX + 2) * 8, y = dy[i], w;
    unsigned char wc, c0;
    deskFill(deskX * 8, y - 3, deskX * 8 + 79, y + 47);
    XCALL(bTile, tile_draw, unsigned char (*)(unsigned char, unsigned int, unsigned char, unsigned char, unsigned char))
        (deskX + 2, y, 2, deskTile(i), didx[i]);
    if (dkind[i] == K_TRASH && trashIsFull) {
        WDrawLine(DRAW_INK, x + 11, y + 4, x + 14, y + 1);
        WDrawLine(DRAW_INK, x + 17, y + 1, x + 20, y + 4);
    }
    if (i == deskSel)
        WInvertRectangle(x + 2, y + 2, x + 29, y + 29);     /* (on the pattern a ring would not show) */
    deskLabel(i, str);
    w = width(str);
    while (w > 70) {
        str[strlen(str) - 1] = 0;
        w = width(str);
    }
    wc = (w + 8 + 7) / 8;
    c0 = (x + 16 - wc * 4) / 8;
    dlc[i] = c0;
    dlw[i] = wc;
    SetPattern(0);
    WRectangle(c0 * 8, y + 32, (c0 + wc) * 8 - 1, y + 47);
    WPutString(str, c0 * 8 + (wc * 8 - w) / 2, y + 34 + baselineOffset);
    WInvertRectangle(c0 * 8, y + 32, (c0 + wc) * 8 - 1, y + 47);
}

/* window w's place with its shadow: gl, gt, gr, gb (the macros once) */
#pragma bss-name ("OVL1BSS")
static unsigned int gl, gt, gr, gb;
#pragma bss-name ("BSS")

static void placeOf(unsigned char w)
{
    gl = L(w);
    gt = T(w);
    gr = RS(w);
    gb = BS(w);
}

/* window w's colours (its place with the shadow) */
static void winColours(unsigned char w)
{
    placeOf(w);
    colours_in(gl, gt, gr, gb);
}

static unsigned char over(unsigned int l, unsigned int t, unsigned int r, unsigned int b,
                          unsigned int al, unsigned int at, unsigned int ar, unsigned int ab)
{
    return l <= ar && al <= r && t <= ab && at <= b;
}

/*---------------------------------------------------------------- a window */

void win_status(unsigned char w)
{
    XCALL(bBars, chrome_status, FB)(w);
}

/* the content and the status bar (and with the clip on only what meets
   it: the scene) */
static void content(unsigned char w)
{
    struct win *p = &wins[w];
    if (w == front) {
        cx0 = L(w) + (p->w >= SIDEMIN ? SIDEW * 8 : 1);
        cy0 = T(w) + CTOP + 2;
        cx1 = R(w) - 1;
        cy1 = B(w) - STATH;
    }
    if (shell_seen(L(w), B(w) - STATH + 1, R(w), B(w)))
        win_status(w);
    if (found_on && w == front) {
        SetPattern(0);
        WRectangle(cx0, cy0 - 2, cx1, cy1 - 1);
        UseFont(FONT_UI);
        XCALL(bFiles, found_draw, FV)();
        UseSystemFont();
    } else
        XCALL(bView, view_draw, FB)(w);
}

/* the content, status and footer (window w shows whole: it is in front),
   then the window's colours */
static void win_content_(unsigned char w)
{
    if (!wins[w].open)
        return;
    content(w);
    winColours(w);
}

/* the whole window, on the screens dispBufferOn selects (with the clip
   on, what meets it), without its colours */
static void drawWin(unsigned char w)
{
    SetPattern(0);
    WRectangle(L(w), T(w), R(w), B(w));
    XCALL(bChrome, chrome_draw, FB)(w);
    content(w);
    XCALL(bBars, chrome_shadow, FB)(w);
}

/*---------------------------------------------------------------- the scene */

/* the desktop in l, t, r, b and everything over it, bottom up, on the
   screens dispBufferOn selects; skip: a window left as it is ($FF none).
   With the kernel's clip rectangle (DECISIONS V81): nothing outside l, t,
   r, b is touched, and what does not meet it is not drawn at all (the
   picture's area widened to whole bytes: what it widens over is drawn
   again as it was) */
void win_scene(unsigned int l, unsigned int t, unsigned int r, unsigned int b, unsigned char skip)
{
    unsigned char i, z, w;
    unsigned int sl = leftMargin, sr = rightMargin, st = windowTopW, sb = windowBottomW;
    if (wallMode == W_PICTURE) {        /* (the picture is drawn in whole bytes) */
        l &= ~7;
        r |= 7;
    }
    if (t < 16)
        t = 16;
    if (l > r || t > b)
        return;
    leftMargin = l;
    rightMargin = r;
    windowTopW = t;
    windowBottomW = b;
    clipOn = 0x80;
    deskFill(l, t, r, b);
    UseFont(FONT_UI);
    for (i = 0; i < ndesk; ++i)
        if (over(l, t, r, b, deskX * 8, dy[i] - 3, deskX * 8 + 79, dy[i] + 47))
            win_deskicon(i);
    UseSystemFont();
    for (z = 0; z < 2; ++z) {
        w = z ? front : front ^ 1;
        if (w >= NWIN || !wins[w].open || w == skip)
            continue;
        placeOf(w);
        if (over(l, t, r, b, gl, gt, gr, gb))
            drawWin(w);
    }
    /* the dock over everything (V83) */
    XCALL(bDock, dock_draw, void (*)(unsigned int, unsigned int, unsigned int, unsigned int))(l, t, r, b);
    clipOn = 0;
    leftMargin = sl;
    rightMargin = sr;
    windowTopW = st;
    windowBottomW = sb;
}

/* the colours (win_colours) of the cards of l, t, r, b alone */
void colours_in(unsigned int l, unsigned int t, unsigned int r, unsigned int b)
{
    colX0 = l >> 3;
    colX1 = r >> 3;
    colY0 = t >> 3;
    colY1 = b >> 3;
    colClip = 1;
    win_colours();
    colClip = 0;
}

/* the colours of the windows' title bands (the one in front on the
   accent: it may have changed) */
static void titles(void)
{
    unsigned char w;
    for (w = 0; w < NWIN; ++w)
        if (wins[w].open)
            colours_in(L(w), T(w), R(w), T(w) + TITLEH - 1);
}

/* l, t, r, b drawn again: into the back screen, then to the foreground */
static void win_repaint_(unsigned int l, unsigned int t, unsigned int r, unsigned int b)
{
    dispBufferOn = ST_WR_BACK;
    win_scene(l, t, r, b, 0xFF);
    dispBufferOn = BOTH;
    WRecoverRectangle(l, t, r, b);
    colours_in(l, t, r, b);
    titles();
}

/* the whole screen but the menus */
static void win_all_(void)
{
    dispBufferOn = BOTH;
    deskLayout();
    XCALL(bDock, dock_layout, FV)();
    trashIsFull = XCALL(bFiles, trash_full, unsigned char (*)(void))();
    /* (the menu bar, full width, its clock: the kernel's, V74) */
    win_scene(0, 16, SW - 1, SH - 1, 0xFF);
    win_colours();
}

/* the dock again where window w (drawn alone) went over it (V83) */
static void dockOver(unsigned char w)
{
    placeOf(w);
    XCALL(bDock, dock_draw, void (*)(unsigned int, unsigned int, unsigned int, unsigned int))(gl, gt, gr, gb);
}

/* window w drawn again where it shows */
static void win_redraw_(unsigned char w)
{
    if (!wins[w].open)
        return;
    if (w == front) {
        dispBufferOn = BOTH;
        drawWin(w);
        dockOver(w);
        winColours(w);
        titles();
        return;
    }
    placeOf(w);
    win_repaint(gl, gt, gr, gb);
}

/* window w alone in l, t, r, b (the kernel's clip), on the screens
   dispBufferOn selects: where it is on top */
static void clipWin(unsigned char w, unsigned int l, unsigned int t, unsigned int r, unsigned int b)
{
    unsigned int sl = leftMargin, sr = rightMargin, st = windowTopW, sb = windowBottomW;
    leftMargin = l;
    rightMargin = r;
    windowTopW = t;
    windowBottomW = b;
    clipOn = 0x80;
    drawWin(w);
    clipOn = 0;
    leftMargin = sl;
    rightMargin = sr;
    windowTopW = st;
    windowBottomW = sb;
}

/* window w come to the front (DECISIONS V81): what the other covered of
   it drawn again (it alone where it is, the scene where its shadow is),
   its title band and status bar (the front window's: bold, the file
   picked up) on both screens, its colours; the rest of it shows already
   as it would be drawn. A window that showed the find's results
   (foundWin) is drawn whole. */
static void win_raise_(unsigned char w)
{
    unsigned char o = w ^ 1;
    unsigned int l, t, r, b, wr, wb;
    if (!wins[w].open)
        return;
    if (foundWin == w + 1) {
        foundWin = 0;
        win_redraw_(w);
        return;
    }
    if (wins[o].open) {
        placeOf(o);
        l = gl, t = gt, r = gr, b = gb;
        placeOf(w);
        if (gl > l)
            l = gl;
        if (gt > t)
            t = gt;
        if (gr < r)
            r = gr;
        if (gb < b)
            b = gb;
        if (l <= r && t <= b) {
            wr = R(w);
            wb = B(w);
            /* (most of it covered: drawn whole, as before) */
            if ((unsigned long)((r - l) >> 3) * ((b - t) >> 3) * 2 > (unsigned long)wins[w].w * (wins[w].h >> 3)) {
                win_redraw_(w);
                return;
            }
            dispBufferOn = BOTH;
            if (l <= wr && t <= wb)
                clipWin(w, l, t, r < wr ? r : wr, b < wb ? b : wb);
            if (r > wr)                 /* (its shadow on the other: the scene) */
                win_repaint_(wr + 1, t, r, b);
            if (b > wb)
                win_repaint_(l, wb + 1, r < wr ? r : wr, b);
        }
    }
    dispBufferOn = BOTH;
    XCALL(bChrome, chrome_title, FB)(w);
    win_status(w);
    dockOver(w);
    winColours(w);
    titles();
}

/* window w (in front) in another view or order: its contents and status
   bar, its colours (the toolbar's toggle is a colour; without colours,
   Classic, inverted: then the whole window) */
static void win_view_(unsigned char w)
{
    if (lookNone) {
        win_redraw_(w);
        return;
    }
    dispBufferOn = BOTH;
    content(w);
    winColours(w);
}

/* the title band of window w, which went behind (on both screens: the
   window in front is drawn whole after it) */
void win_behind(unsigned char w)
{
    if (w >= NWIN || !wins[w].open)
        return;
    dispBufferOn = BOTH;
    XCALL(bChrome, chrome_title, FB)(w);
}


void win_all(void)
{
    unsigned char busy = play_busy;     /* (the script waits while it draws) */
    play_busy = 1;
    win_all_();
    play_busy = busy;
}

void win_redraw(unsigned char w)
{
    unsigned char busy = play_busy;     /* (the script waits while it draws) */
    play_busy = 1;
    win_redraw_(w);
    play_busy = busy;
}

void win_raise(unsigned char w)
{
    unsigned char busy = play_busy;     /* (the script waits while it draws) */
    play_busy = 1;
    win_raise_(w);
    play_busy = busy;
}

void win_view(unsigned char w)
{
    unsigned char busy = play_busy;     /* (the script waits while it draws) */
    play_busy = 1;
    win_view_(w);
    play_busy = busy;
}

void win_content(unsigned char w)
{
    unsigned char busy = play_busy;     /* (the script waits while it draws) */
    play_busy = 1;
    win_content_(w);
    play_busy = busy;
}

void win_repaint(unsigned int l, unsigned int t, unsigned int r, unsigned int b)
{
    unsigned char busy = play_busy;     /* (the script waits while it draws) */
    play_busy = 1;
    win_repaint_(l, t, r, b);
    play_busy = busy;
}
