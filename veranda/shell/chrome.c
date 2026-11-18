/* Veranda shell: a window's chrome (overlay 8; Veranda Look 2, DECISIONS
   V77).

   A window, from its top (its cards: x and width in cards, y and height
   in multiples of 8, so every band is whole cards and takes one colour):
   - the title band (16 rows, the theme's accent; chrome for the window
     behind): the folder's name centred, bold in front; minimise, zoom
     and close at the right, as glyphs;
   - the toolbar (24 rows, chrome): back and forward, the path as a
     breadcrumb (each crumb a folder to go to), the icons and list
     toggle (the view shown on the selection colour), the search field
     (2 card rows of the paper; the windows at least 44 cards wide);
   - the body: the sidebar (12 cards of the sidebar colour, in a window
     at least SIDEMIN cards wide): the places (Veranda, Apps, Documents,
     Mods; the card and the trash under "devices"), the one shown on the
     selection colour; the contents (view.c);
   - the status bar (16 rows, chrome): the page (when there are several:
     the arrows at the left go to the one before and after), the items
     and the selection, the card's free space, the resize grip;
   - the shadow: an ink dither 4 pixels wide, right and below.
   The colours are cards of the colour map (ColorRect); the ink is the
   one ink. The Classic theme has no colours: the toggle shown inverted,
   the place in bold. */
#include <veranda.h>
#include <string.h>
#include <theme.h>
#include "shell.h"
#include "winp.h"
#include "tiles.h"
#pragma code-name ("OVERLAY8")
#pragma bss-name ("OVL8BSS")

#define NCRUMB 8
static unsigned int crumbX[NWIN][NCRUMB];       /* each crumb's right end */
static unsigned char crumbLen[NWIN][NCRUMB];    /* the path's characters up to it */
static unsigned char ncrumb[NWIN];

#pragma bss-name ("BSS")

const char *const placeName[NPLACES] = {
    "Veranda", "Apps", "Documents", "Mods", "SD card", "Trash"
};
/* the Mods place's folder (V84: File > Set as Mods folder; dir.c reads
   and writes MODS.CFG) */
char modsDir[PATHMAX + 1] = "/MODS";
const char *const placePath[NPLACES] = {
    VR_SYSDIR, VR_APPSDIR, VR_DOCSDIR, modsDir, "/", VR_TRASHDIR
};
const unsigned char placeMini[NPLACES] = { M_HOME, M_APPS, M_DOC, M_NOTE, M_CARD, M_TRASH };
const char sPlaces[] = "PLACES";
const char sDevices[] = "DEVICES";
static const char sSearch[] = "Search";

const char *chrome_place(unsigned char i)
{
    return i == 5 && trash[0] ? trash : placePath[i];
}

#pragma rodata-name (push, "OVL8RO")
static const char noMods[] = "There is no Mods folder.";
static const char noOpen[] = "Cannot open the folder.";
#pragma rodata-name (pop)

/* a place of the sidebar: Veranda, Apps, Documents, Mods, the card, the
   trash (made when missing); in window w (moved here from main.c, V84) */
void chrome_goplace(unsigned char w, unsigned char i)
{
    const char *p;
    if (i == 5) {
        XCALL(bFiles, trash_path, FV)();
        ChDir("/");
        MkDir(trash + 1);
    }
    p = i < 3 ? vr_cd(i == 0 ? VR_SYS : i == 1 ? VR_APPS : VR_DOCS) : chrome_place(i);
    ChDir(curDirName);
    if (strcmp(p, wins[w].path) && !shell_goto(w, p)) {
        strcpy(msg, i == 3 ? noMods : noOpen);
        shell_warn(msg);                /* (below $A000 for the kernel) */
    }
}

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
        q = chrome_place(i);
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

/* s cut to fit in w pixels (with "..." when cut); its width */
static unsigned int fit(char *s, unsigned int w)
{
    unsigned char l, c;
    unsigned int x = 0;
    for (l = 0; (c = s[l]) != 0; ++l) {
        c = GetCharWidth(c);
        if (x + c > w)
            break;
        x += c;
    }
    if (!s[l])
        return x;
    while (l && x + 3 * GetCharWidth('.') > w)
        x -= GetCharWidth(s[--l]);
    strcpy(s + l, "...");
    return x + 3 * GetCharWidth('.');
}

static void put(const char *s, unsigned int x, unsigned int y)
{
    WPutString((void *)s, x, y + baselineOffset);
}

static void bold(const char *s, unsigned int x, unsigned int y)
{
    currentMode = SET_BOLD;
    put(s, x, y);
    currentMode = 0;
}

static unsigned int boldW(const char *s)
{
    return width(s) + strlen(s);
}

/* the name of the folder p's first n characters: a place's, or its own */
static void nameOf(const char *p, unsigned char n, char *out)
{
    unsigned char i = placeOf(p, n), k;
    if (i != 0xFF) {
        strcpy(out, placeName[i]);
        return;
    }
    for (k = n; k && p[k - 1] != '/'; --k)
        ;
    memcpy(out, p + k, n - k);
    out[n - k] = 0;
}

/*---------------------------------------------------------------- the title band */

void chrome_title(unsigned char w)
{
    unsigned int l = L(w), r = R(w), t = T(w), bx, tw, mx;
    struct win *p = &wins[w];
    SetPattern(0);
    WRectangle(l + 1, t + 1, r - 1, t + TITLEH - 2);
    WHorizontalLine(0xFF, l, r, t + TITLEH - 1);
    /* close, zoom, minimise */
    bx = r - 14;
    WDrawLine(DRAW_INK, bx, t + 4, bx + 7, t + 11);
    WDrawLine(DRAW_INK, bx + 7, t + 4, bx, t + 11);
    bx -= 18;
    WFrameRectangle(0xFF, bx, t + 4, bx + 7, t + 11);
    bx -= 18;
    WHorizontalLine(0xFF, bx, bx + 7, t + 11);
    /* the name */
    UseFont(FONT_UI);
    nameOf(p->path, strlen(p->path), str);
    mx = r - l > 120 ? r - l - 120 : 8;
    tw = fit(str, mx);
    if (w == front)
        bold(str, (l + r - tw - strlen(str)) / 2, t + 2);
    else
        put(str, (l + r - tw) / 2, t + 2);
    UseSystemFont();
}

/*---------------------------------------------------------------- the toolbar */

static void chevron(unsigned int x, unsigned int y, signed char d)
{
    /* thick: two lines a side */
    WDrawLine(DRAW_INK, x + 3 * d, y + 5, x - 2 * d, y);
    WDrawLine(DRAW_INK, x + 3 * d, y + 5, x - 2 * d, y + 10);
    WDrawLine(DRAW_INK, x + 3 * d + 1, y + 5, x - 2 * d + 1, y);
    WDrawLine(DRAW_INK, x + 3 * d + 1, y + 5, x - 2 * d + 1, y + 10);
}

static void crumbs(unsigned char w, unsigned int y0, unsigned int xmax)
{
    struct win *p = &wins[w];
    unsigned int x = L(w) + 50, tw;
    unsigned char n = 0, i = 0, l = strlen(p->path), last;
    UseFont(FONT_UI);
    for (;;) {
        /* the crumb up to i (the root: 1) */
        if (i == 0)
            i = 1;
        else {
            while (i < l && p->path[i] != '/')
                ++i;
        }
        last = i >= l;
        nameOf(p->path, i, str);
        if (!str[0])
            strcpy(str, placeName[4]);
        tw = (last ? boldW(str) : width(str));
        if (x + tw > xmax) {
            tw = fit(str, xmax > x + 8 ? xmax - x - 8 : 0);
            last = 1;
        }
        if (last)
            bold(str, x, y0 + 6);
        else
            put(str, x, y0 + 6);
        x += tw;
        crumbX[w][n] = x + 6;
        crumbLen[w][n++] = i;
        if (last || n >= NCRUMB)
            break;
        WDrawLine(DRAW_INK, x + 5, y0 + 9, x + 8, y0 + 12);
        WDrawLine(DRAW_INK, x + 8, y0 + 12, x + 5, y0 + 15);
        x += 14;
        ++i;
    }
    ncrumb[w] = n;
    UseSystemFont();
}

static void toolbar(unsigned char w)
{
    struct win *p = &wins[w];
    unsigned int l = L(w), r = R(w), y0 = T(w) + TITLEH, bx, by, x0;
    unsigned char vx = toggles(w), i, a, b;
    SetPattern(0);
    WRectangle(l + 1, y0, r - 1, y0 + TOOLH - 2);
    WHorizontalLine(0xFF, l, r, y0 + TOOLH - 1);
    chevron(l + 14, y0 + 6, -1);
    chevron(l + 32, y0 + 6, 1);
    crumbs(w, y0, vx * 8 - 8);
    /* icons and list */
    SetPattern(1);
    for (i = 0; i < 2; ++i) {
        bx = (vx + i * 3) * 8 + 6;
        by = y0 + 6;
        if (!i) {
            for (a = 0; a < 8; a += 7)
                for (b = 0; b < 8; b += 7)
                    WRectangle(bx + a, by + b, bx + a + 4, by + b + 4);
        } else
            for (b = 0; b < 9; b += 4) {
                WRectangle(bx, by + b + 1, bx + 1, by + b + 2);
                WRectangle(bx + 3, by + b + 1, bx + 11, by + b + 2);
            }
    }
    WFrameRectangle(0xFF, vx * 8, y0 + 3, (vx + 6) * 8 - 1, y0 + 20);
    WVerticalLine(0xFF, (vx + 3) * 8, y0 + 3, y0 + 20);
    if (lookNone && (p->view == V_ICON || p->view == V_LIST)) {
        bx = (vx + (p->view == V_LIST ? 3 : 0)) * 8;
        WInvertRectangle(bx + 1, y0 + 4, bx + 23, y0 + 19);
    }
    /* the search field */
    if (hasSearch(w)) {
        x0 = (p->x + p->w - 15) * 8;
        SetPattern(0);
        WRectangle(x0, y0, x0 + 103, y0 + 15);
        shell_rframe(x0, y0 + 1, x0 + 103, y0 + 14, 6);
        shell_rframe(x0 + 6, y0 + 4, x0 + 12, y0 + 10, 3);
        WDrawLine(DRAW_INK, x0 + 11, y0 + 10, x0 + 14, y0 + 13);
        if (p->find[0]) {
            UseFont(FONT_UI);
            strcpy(str, p->find);
            fit(str, 80);
            put(str, x0 + 19, y0 + 2);
            UseSystemFont();
        } else
            put(sSearch, x0 + 19, y0 + 4);
    }
}

/*---------------------------------------------------------------- the window */

/* (the scene: with the kernel's clip on, only the bands that meet it,
   DECISIONS V81) */
void chrome_draw(unsigned char w)
{
    unsigned int l = L(w), t = T(w), r = R(w), b = B(w);
    WFrameRectangle(0xFF, l, t, r, b);
    if (shell_seen(l, t, r, t + TITLEH - 1))
        chrome_title(w);
    if (shell_seen(l, t + TITLEH, r, t + CTOP - 1))
        toolbar(w);
    if (side(w) && shell_seen(l, t + CTOP, l + SIDEW * 8 - 1, b - STATH))
        XCALL(bBars, bars_sidebar, FB)(w);
}

/*---------------------------------------------------------------- presses */

/* place i's card row in window w's sidebar, 0 if it does not fit (as
   bars.c draws it) */
static unsigned char placeRow(unsigned char w, unsigned char i)
{
    unsigned char y = (T(w) + CTOP) / 8 + 2 + 2 * i + (i >= 4 ? 3 : 0);
    return (y + 2) * 8 <= B(w) - STATH + 1 ? y : 0;
}

static unsigned char pages(unsigned char w)
{
    struct win *p = &wins[w];
    unsigned char per = XCALL(bView, view_per, RB)(w);
    return p->view != V_LIST && per && p->n > per;
}

unsigned char chrome_hit(unsigned char w, unsigned int x, unsigned int y)
{
    unsigned int l = L(w), r = R(w), t = T(w), b = B(w), vx;
    unsigned char i, pg;
    if (y < t + TITLEH)
        return x >= r - 17 ? H_CLOSE : x >= r - 35 ? H_ZOOM : x >= r - 53 ? H_MIN : H_TITLE;
    if (y < t + CTOP) {
        vx = toggles(w) * 8;
        if (x >= l + 6 && x <= l + 22)
            return H_BACK;
        if (x >= l + 24 && x <= l + 42)
            return H_FWD;
        if (x >= vx && x < vx + 48 && y >= t + TITLEH + 3 && y <= t + TITLEH + 20)
            return x < vx + 24 ? H_ICONS : H_LIST;
        if (hasSearch(w) && x >= (wins[w].x + wins[w].w - 15) * 8 && x < (wins[w].x + wins[w].w - 2) * 8
            && y < t + TITLEH + 16)
            return H_SEARCH;
        if (x >= l + 50)
            for (i = 0; i < ncrumb[w]; ++i)
                if (x < crumbX[w][i])
                    return H_CRUMB + i;
        return H_NONE;
    }
    if (y > b - STATH) {
        if (x >= r - 15)
            return H_SIZE;
        if (x < l + 96 && pages(w))
            return x < l + 48 ? H_PREV : H_NEXT;
        return H_NONE;
    }
    if (side(w) && x < l + SIDEW * 8) {
        for (i = 0; i < NPLACES; ++i)
            if ((pg = placeRow(w, i)) != 0 && y >= pg * 8 && y < pg * 8 + 16)
                return H_PLACE + i;
        return H_NONE;
    }
    return H_BODY;
}

/* the crumb's path length (mouse.c, through the press) */
unsigned char chrome_crumb(unsigned char w, unsigned char i)
{
    return crumbLen[w][i];
}
