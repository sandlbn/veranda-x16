/* Veranda shell: a window's bars (overlay 9 with tiles.c; Veranda Look 2,
   DECISIONS V77): the sidebar, the status bar, the shadow, the colours of
   a window's chrome, the zoom box (chrome.c has the rest: the title
   band, the toolbar, what a press is). */
#include <veranda.h>
#include <string.h>
#include <theme.h>
#include "shell.h"
#include "winp.h"
#include "tiles.h"
#pragma code-name ("OVERLAY9")

static unsigned char lower(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

/* the place whose folder is p's first n characters, $FF none */
static unsigned char placeOf(const char *p, unsigned char n)
{
    unsigned char i, k;
    const char *q;
    for (i = 0; i < NPLACES; ++i) {
        q = i == 5 && trash[0] ? trash : placePath[i];
        for (k = 0; k < n && q[k] && lower(q[k]) == lower(p[k]); ++k)
            ;
        if (k == n && !q[k])
            return i;
    }
    return 0xFF;
}

static unsigned char side(unsigned char w)
{
    return wins[w].w >= SIDEMIN;
}

static unsigned char hasSearch(unsigned char w)
{
    return wins[w].w >= 44;
}

/* the toggles' first card */
static unsigned char toggles(unsigned char w)
{
    return wins[w].x + wins[w].w - (hasSearch(w) ? 22 : 7);
}

static unsigned int width(const char *s)
{
    unsigned int w = 0;
    while (*s)
        w += GetCharWidth(*s++);
    return w;
}

static void put(const char *s, unsigned int x, unsigned int y)
{
    WPutString((void *)s, x, y + baselineOffset);
}

/*---------------------------------------------------------------- the sidebar */

/* place i's card row in window w's sidebar, 0 if it does not fit */
static unsigned char placeRow(unsigned char w, unsigned char i)
{
    unsigned char y = (T(w) + CTOP) / 8 + 2 + 2 * i + (i >= 4 ? 3 : 0);
    return (y + 2) * 8 <= B(w) - STATH + 1 ? y : 0;
}

static unsigned char shownPlace(unsigned char w)
{
    struct win *p = &wins[w];
    return placeOf(p->path, strlen(p->path));
}

/* the sidebar (chrome_draw) */
void bars_sidebar(unsigned char w)
{
    unsigned int l = L(w), y0 = T(w) + CTOP, sx = l + SIDEW * 8 - 1;
    unsigned char i, y, sel = shownPlace(w);
    WVerticalLine(0xFF, sx, y0, B(w) - STATH);
    put(sPlaces, l + 8, y0 + 5);
    for (i = 0; i < NPLACES; ++i) {
        y = placeRow(w, i);
        if (!y)
            break;
        if (i == 4)
            put(sDevices, l + 8, y * 8 - 15);
        XCALL(bTile, mini_draw, void (*)(unsigned int, unsigned int, unsigned char))(l + 8, y * 8 + 3, placeMini[i]);
        UseFont(FONT_UI);
        if (i == sel)
            currentMode = SET_BOLD;
        put(placeName[i], l + 22, y * 8 + 2);
        currentMode = 0;
        UseSystemFont();
    }
}

/*---------------------------------------------------------------- the status bar */

static unsigned char pages(unsigned char w, unsigned char *pg)
{
    struct win *p = &wins[w];
    unsigned char per = XCALL(bView, view_per, RB)(w), n;
    if (p->view == V_LIST || !per)
        return 1;
    n = (p->n + per - 1) / per;
    *pg = p->first / per + 1;
    return n ? n : 1;
}

static char *dec(char *o, unsigned long n)
{
    num(n, o);
    return o + strlen(o);
}

void chrome_status(unsigned char w)
{
    struct win *p = &wins[w];
    unsigned int l = L(w), r = R(w), b = B(w), sy = b - STATH + 1, tx;
    unsigned char np, pg = 1;
    char *o;
    SetPattern(0);
    WRectangle(l + 1, sy + 1, r - 1, b - 1);
    WHorizontalLine(0xFF, l, r, sy);
    /* the resize grip */
    WDrawLine(DRAW_INK, r - 10, b - 2, r - 2, b - 10);
    WDrawLine(DRAW_INK, r - 6, b - 2, r - 2, b - 6);
    UseFont(FONT_UI);
    tx = l + (side(w) ? SIDEW * 8 : 0) + 6;
    np = pages(w, &pg);
    if (np > 1) {
        WDrawLine(DRAW_INK, l + 10, sy + 7, l + 13, sy + 4);
        WDrawLine(DRAW_INK, l + 10, sy + 7, l + 13, sy + 10);
        WDrawLine(DRAW_INK, l + 86, sy + 7, l + 83, sy + 4);
        WDrawLine(DRAW_INK, l + 86, sy + 7, l + 83, sy + 10);
        o = dec(str, pg);
        strcpy(o, " of ");
        dec(o + 4, np);
        put(str, l + 48 - width(str) / 2, sy + 2);
        if (tx < l + 100)
            tx = l + 100;
    }
    o = dec(msg, p->nfiles);
    strcpy(o, p->nfiles == 1 ? " item, " : " items, ");
    o += strlen(o);
    *o++ = p->sel != 0xFF && !(p->sel == 0 && p->path[1]) ? '1' : '0';
    strcpy(o, " selected");
    put(msg, tx, sy + 2);
    tx += width(msg) + 16;
    if (w == front)
        XCALL(bFiles, files_title, void (*)(unsigned int, unsigned int))(tx, sy + 2);
    if (cardFree != 0xFFFFFFFFUL && r - l > 360) {
        if (cardFree >= 10UL << 20) {
            o = dec(str, cardFree >> 20);
            strcpy(o, " MB free");
        } else {
            o = dec(str, cardFree >> 10);
            strcpy(o, " KB free");
        }
        put(str, r - 18 - width(str), sy + 2);
    }
    UseSystemFont();
}


void chrome_shadow(unsigned char w)
{
    unsigned int r = R(w), b = B(w), rs = RS(w), bs = BS(w);
    unsigned char pic = wallMode == W_PICTURE;  /* (on the picture: darker, V78) */
    SetPattern(2);
    if (rs > r && shell_seen(r + 1, T(w) + 6, rs, bs)) {
        if (pic)
            XCALL(bWall, wall_shade, void (*)(unsigned int, unsigned int, unsigned int, unsigned int))(r + 1, T(w) + 6, rs, bs);
        else
            WRectangle(r + 1, T(w) + 6, rs, bs);
    }
    if (bs > b && shell_seen(L(w) + 6, b + 1, rs, bs)) {
        if (pic)
            XCALL(bWall, wall_shade, void (*)(unsigned int, unsigned int, unsigned int, unsigned int))(L(w) + 6, b + 1, rs, bs);
        else
            WRectangle(L(w) + 6, b + 1, rs, bs);
    }
}

void chrome_colours(unsigned char w)
{
    struct win *p = &wins[w];
    unsigned char x = p->x, cw = p->w, y = p->y >> 3, h = p->h >> 3, i;
    unsigned char sel = theme.hilite ? theme.hilite : SELECT;
    look_rect(x, cw, y, h, 0);
    look_rect(x, cw, y, 2, w == front ? theme.accent : CHROME);
    look_rect(x, cw, y + 2, 3, CHROME);
    if (p->view == V_ICON || p->view == V_LIST)
        look_rect(toggles(w) + (p->view == V_LIST ? 3 : 0), 3, y + 2, 3, sel);
    if (hasSearch(w))
        look_rect(x + cw - 15, 13, y + 2, 2, 0);
    if (side(w)) {
        look_rect(x, SIDEW, y + 5, h - 7, SIDEBAR);
        i = shownPlace(w);
        if (i != 0xFF && (i = placeRow(w, i)) != 0)
            look_rect(x, SIDEW, i, 2, sel);
    }
    look_rect(x, cw, y + h - 2, 2, CHROME);
}

/*---------------------------------------------------------------- the zoom box */

/* the window as large as the desktop beside its icons, and
   back to where it was */
static void win_zoom_(unsigned char w)
{
    struct win *p = &wins[w];
    unsigned int l = L(w), t = T(w), r = RS(w), b = BS(w);
    unsigned char zx = p->x, zw = p->w;
    unsigned int zy = p->y, zh = p->h;
    if (p->zw) {
        p->x = p->zx;
        p->w = p->zw;
        p->y = p->zy;
        p->h = p->zh;
        p->zw = 0;
    } else {
        p->x = SW > 320 ? 1 : 0;
        p->w = deskX - p->x - 1;
        p->y = SH > 240 ? 24 : 16;
        p->h = SH - p->y - 8;
        p->zx = zx, p->zw = zw, p->zy = zy, p->zh = zh;
    }
    XCALL(bWin, win_clamp, FB)(w);
    if (L(w) < l)
        l = L(w);
    if (T(w) < t)
        t = T(w);
    if (RS(w) > r)
        r = RS(w);
    if (BS(w) > b)
        b = BS(w);
    if (w)
        deskDirty = 1;
    XCALL(bWin, win_repaint, void (*)(unsigned int, unsigned int, unsigned int, unsigned int))(l, t, r, b);
    strcpy(msg, "SHELL WINDOW ");
    num(w, msg + 13);
    strcat(msg, p->zw ? " ZOOMED " : " UNZOOMED ");
    num(p->w * 8, msg + strlen(msg));
    strcat(msg, " ");
    num(p->h, msg + strlen(msg));
    LogText(msg);
}

void win_zoom(unsigned char w)
{
    unsigned char busy = play_busy;
    play_busy = 1;
    win_zoom_(w);
    play_busy = busy;
}
