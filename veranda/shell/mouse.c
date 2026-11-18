/* Veranda shell: the mouse on the desktop and in the windows (overlay
   6; DECISIONS V70): presses, double clicks, dragging a window's title
   bar or its corner and an icon (an outline on the foreground only), and
   the drops (win.c says what a drop does). */
#include <veranda.h>
#include <string.h>
#include "shell.h"
#include "winp.h"
#pragma code-name ("OVERLAY6")

/* a drag */
enum { D_NONE, D_PRESS, D_ICON, D_MOVE, D_SIZE, D_DESK, D_DESKDRAG };
static unsigned char drag, dwin, dent, dmods;
static unsigned int px, py;             /* where the press was */
static unsigned int ol, ot, or_, ob;    /* the outline shown */
static unsigned char outlined;
static unsigned int offx, offy;
static const char *ttag;                /* what a press or a release did (the gates' times) */

/* the emulator's cycle counter (a read of $9FB8 latches it) */
static unsigned long cycles(void)
{
    unsigned long c;
    unsigned char *q = (unsigned char *)&c;
    q[0] = *(volatile unsigned char *)0x9FB8;
    q[1] = *(volatile unsigned char *)0x9FB9;
    q[2] = *(volatile unsigned char *)0x9FBA;
    q[3] = *(volatile unsigned char *)0x9FBB;
    return c;
}

/* testing: "TIME <what> <cycles>" for a press or a release that did
   something (DECISIONS V81) */
static void tlog(unsigned long c)
{
    if (!testing || !ttag)
        return;
    c = cycles() - c;
    strcpy(msg, "TIME ");
    strcat(msg, ttag);
    strcat(msg, " ");
    num(c, msg + strlen(msg));
    LogText(msg);
}

/*---------------------------------------------------------------- the outline */

static void xorFrame(void)
{
    dispBufferOn = ST_WR_FORE;
    WInvertRectangle(ol, ot, or_, ot);
    WInvertRectangle(ol, ob, or_, ob);
    if (ob > ot + 1) {
        WInvertRectangle(ol, ot + 1, ol, ob - 1);
        WInvertRectangle(or_, ot + 1, or_, ob - 1);
    }
    dispBufferOn = BOTH;
}

static void outline(unsigned int l, unsigned int t, unsigned int r, unsigned int b)
{
    if (outlined && l == ol && t == ot && r == or_ && b == ob)
        return;
    if (outlined)
        xorFrame();
    ol = l, ot = t, or_ = r, ob = b;
    xorFrame();
    outlined = 1;
}

static void unoutline(void)
{
    if (outlined)
        xorFrame();
    outlined = 0;
}

/*---------------------------------------------------------------- hits */

/* the window at x, y (the front one first), $FF none */
static unsigned char winAt(unsigned int x, unsigned int y)
{
    unsigned char z, w;
    for (z = 0; z < 2; ++z) {
        w = z ? front ^ 1 : front;
        if (w < NWIN && wins[w].open && x >= L(w) && x <= R(w) && y >= T(w) && y <= B(w))
            return w;
    }
    return 0xFF;
}

/* a desktop icon: its tile (cards deskX + 2 to + 5) and its label */
static unsigned char deskAt(unsigned int x, unsigned int y)
{
    unsigned char i;
    if (x < deskX * 8 + 8 || x > deskX * 8 + 71)
        return 0xFF;
    for (i = 0; i < ndesk; ++i)
        if (y >= dy[i] && y <= dy[i] + 47)
            return i;
    return 0xFF;
}

static void deskColours(unsigned char i)
{
    XCALL(bWin, colours_in, void (*)(unsigned int, unsigned int, unsigned int, unsigned int))
        (deskX * 8 - 8, dy[i] - 3, deskX * 8 + 79, dy[i] + 47);    /* (a label may start a card left) */
}

static void deskSelect(unsigned char i)
{
    unsigned char o = deskSel;
    deskSel = i;
    dispBufferOn = BOTH;
    UseFont(FONT_UI);
    if (o != 0xFF && o < ndesk && winAt(deskX * 8 + 32, dy[o] + 16) == 0xFF)
        XCALL(bWin, win_deskicon, FB)(o);
    if (i != 0xFF && winAt(deskX * 8 + 32, dy[i] + 16) == 0xFF)
        XCALL(bWin, win_deskicon, FB)(i);
    UseSystemFont();
    /* the colours of the two icons' cards (DECISIONS V81) */
    if (o != 0xFF && o < ndesk)
        deskColours(o);
    if (i != 0xFF && i != o)
        deskColours(i);
}

/* a double click on a desktop icon */
static void deskOpen(unsigned char i)
{
    switch (dkind[i]) {
    case K_CARD:
        {
            unsigned char w = !wins[0].open ? 0 : !wins[1].open ? 1 : front ^ 1;
            strcpy(wins[w].path, "/");
            wins[w].first = 0;
            wins[w].sel = 0xFF;
            if (w)
                deskDirty = 1;
            wins[w].find[0] = 0;
            XCALL(bWin, win_behind, FB)(front);
            shell_front(w);
            shell_hist(w);
            XCALL(bOps, win_open, FB)(w);
        }
        break;
    case K_TRASH:
        shell_trash();
        break;
    case K_PRINTER:
        shell_say("Drop a picture (BMP or Paint) here to print it to a PDF.");
        break;
    default:
        shell_pin_start(didx[i]);
    }
}

/* options/unpin: the pin selected on the desktop, else the last */
void win_unpin(void)
{
    unsigned char i = deskSel != 0xFF && dkind[deskSel] == K_PIN ? didx[deskSel] : npins - 1;
    deskSel = 0xFF;
    XCALL(bDir, pin_remove, FB)(i);
    XCALL(bWin, win_all, FV)();
}

/*---------------------------------------------------------------- drops */

/* the dragged entry (dwin, dent: E) dropped at x, y */
static void drop(unsigned int x, unsigned int y)
{
    unsigned char w = winAt(x, y), s, move = (dmods & 3) != 0, i;
    struct win *p;
    static char to[PATHMAX + 2], nm[NAMELEN + 2];
    ent_get(dwin, dent);
    strcpy(nm, E.name);
    if (w != 0xFF) {
        p = &wins[w];
        s = y >= T(w) + CTOP && y < B(w) - FOOTH && !(found_on && w == front)
            ? XCALL(bView, view_hit, unsigned char (*)(unsigned char, unsigned int, unsigned int))(w, x, y) : 0xFF;
        strcpy(to, p->path);
        if (s < p->n) {
            ent_get(w, p->order[s]);
            if (E.type == 'U' || E.type == 'D') {
                if (w == dwin && p->order[s] == dent)
                    return;
                if (E.type == 'U') {
                    i = strlen(to);
                    while (i > 1 && to[--i] != '/')
                        ;
                    to[i ? i : 1] = 0;
                } else {
                    i = strlen(to);
                    if (i + strlen(E.name) + 2 > PATHMAX) {
                        shell_warn("The path is too long.");
                        return;
                    }
                    if (i > 1)
                        to[i++] = '/';
                    strcpy(to + i, E.name);
                }
                move ^= 1;                      /* into a folder: a move */
                goto doit;
            }
        }
        if (w == dwin)
            return;
        goto doit;
    }
    i = deskAt(x, y);
    if (i == 0xFF) {
        ent_get(dwin, dent);
        if (E.geos == APPLICATION) {
            XCALL(bDir, pin_add, FV)();
            XCALL(bWin, win_all, FV)();
        }
        return;
    }
    ent_get(dwin, dent);
    switch (dkind[i]) {
    case K_TRASH:
        XCALL(bFiles, files_delete, void (*)(const char *, char, unsigned char))(nm, E.type, 0);
        return;
    case K_PRINTER:
        XCALL(bPrint, print_drop, void (*)(const char *))(nm);
        return;
    case K_CARD:
        strcpy(to, "/");
        break;
    default:
        return;
    }
doit:
    ent_get(dwin, dent);
    XCALL(bFiles, files_drop, void (*)(const char *, const char *, char, const char *, unsigned char))
        (wins[dwin].path, nm, E.type, to, move);
}

/*---------------------------------------------------------------- the mouse */

static void release_(void)
{
    unsigned int x = mouseXPos, y = mouseYPosW;
    unsigned char d = drag, w = dwin;
    unsigned int nx, ny;
    drag = D_NONE;
    unoutline();
    switch (d) {
    case D_ICON:
        ttag = "DROP";
        drop(x, y);
        break;
    case D_MOVE:
        nx = x - offx;
        ny = y - offy;
        if ((int)nx < 0)
            nx = 0;
        nx = (nx + 4) >> 3;
        ny = (ny + 4) & 0xFFF8;
        if (nx + wins[w].w > SW / 8)
            nx = SW / 8 - wins[w].w;
        if ((int)ny < 16)
            ny = 16;
        if (ny + wins[w].h > SH)
            ny = SH - wins[w].h;
        if (nx != wins[w].x || ny != wins[w].y)
            ttag = "MOVE";
        XCALL(bOps, win_move, void (*)(unsigned char, unsigned char, unsigned int))(w, nx, ny);
        break;
    case D_SIZE:
        ttag = "RESIZE";
        nx = x > L(w) ? (x - L(w) + 8) >> 3 : 0;
        ny = y > T(w) ? ((y - T(w) + 8) & 0xFFF8) : 0;
        XCALL(bOps, win_resize, void (*)(unsigned char, unsigned char, unsigned int))(w, nx, ny);
        break;
    case D_DESKDRAG:
        if (dkind[dent] == K_PIN && deskAt(x, y) != 0xFF && dkind[deskAt(x, y)] == K_TRASH) {
            deskSel = 0xFF;
            XCALL(bDir, pin_remove, FB)(didx[dent]);
            XCALL(bWin, win_all, FV)();
        }
        break;
    }
}

static void release(void)
{
    unsigned long c = cycles();
    ttag = 0;
    release_();
    tlog(c);
}

/* idle: a drag's outline (the clock: the kernel's menu bar, V74) */
void win_idle(void)
{
    static unsigned char rWas;
    unsigned int x = mouseXPos, y = mouseYPosW;
    unsigned char w = dwin;
    int dx, dy_;
    if ((mouseButtons ^ rWas) & 2) {    /* the right button (the kernel's
                                           presses are the left one's): the
                                           dock's menu (V83) */
        rWas = mouseButtons & 2;
        if (rWas && drag == D_NONE && y >= 16)
            XCALL(bDock, dock_press, unsigned char (*)(unsigned int, unsigned int, unsigned char))(x, y, winAt(x, y));
        return;
    }
    if (drag == D_NONE)
        return;
    if (mouseData & 0x80) {
        release();
        return;
    }
    dx = x - px;
    dy_ = y - py;
    switch (drag) {
    case D_PRESS:
    case D_DESK:
        if (dx > 3 || dx < -3 || dy_ > 3 || dy_ < -3) {
            drag = drag == D_PRESS ? D_ICON : D_DESKDRAG;
            dmods = keyMods;
        } else
            break;
        /* (on) */
    case D_ICON:
    case D_DESKDRAG:
        if (y < 16)
            y = 16;
        outline(x > 12 ? x - 12 : 0, y > 26 ? y - 10 : 16, x + 11 < SW ? x + 11 : SW - 1,
                y + 10 < SH ? y + 10 : SH - 1);
        dmods |= keyMods;
        break;
    case D_MOVE:
        dx = x - offx;
        dy_ = y - offy;
        if (dx < 0)
            dx = 0;
        if (dx + wins[w].w * 8 > SW)
            dx = SW - wins[w].w * 8;
        if (dy_ < 16)
            dy_ = 16;
        if (dy_ + wins[w].h > SH)
            dy_ = SH - wins[w].h;
        dx &= 0xFFF8;
        dy_ &= 0xFFF8;
        outline(dx, dy_, dx + wins[w].w * 8 - 1, dy_ + wins[w].h - 1);
        break;
    case D_SIZE:
        if (x < L(w) + 191)
            x = L(w) + 191;
        if (y < T(w) + 111)
            y = T(w) + 111;
        outline(L(w), T(w), x | 7, y | 7);
        break;
    }
}

static void press_(void)
{
    unsigned int x = mouseXPos, y = mouseYPosW;
    unsigned char w, s, i;
    struct win *p;

    if (mouseData & 0x80) {
        if (drag != D_NONE)
            release();
        ttag = 0;
        return;
    }
    if (y < 16)
        return;
    px = x;
    py = y;
    w = winAt(x, y);
    if (XCALL(bDock, dock_press, unsigned char (*)(unsigned int, unsigned int, unsigned char))(x, y, w)) {
        ttag = "DOCK";
        return;
    }
    if (w == 0xFF) {
        i = deskAt(x, y);
        if (i != 0xFF && i == deskSel && dblClickCount) {
            dblClickCount = 0;
            ttag = "DESKOPEN";
            deskOpen(i);
            return;
        }
        ttag = "DESKSELECT";
        deskSelect(i);
        if (i != 0xFF) {
            dblClickCount = CLICK_COUNT;
            drag = D_DESK;
            dent = i;
        }
        return;
    }
    if (deskSel != 0xFF)
        deskSelect(0xFF);
    ttag = w == front ? 0 : "FRONT";
    XCALL(bOps, win_front, FB)(w);
    p = &wins[w];
    dwin = w;
    s = XCALL(bChrome, chrome_hit, unsigned char (*)(unsigned char, unsigned int, unsigned int))(w, x, y);
    if (s >= H_PLACE) {
        ttag = "PLACE";
        XCALL(bChrome, chrome_goplace, FBB)(w, s - H_PLACE);
        return;
    }
    if (s >= H_CRUMB) {
        shell_crumb(w, XCALL(bChrome, chrome_crumb, unsigned char (*)(unsigned char, unsigned char))(w, s - H_CRUMB));
        return;
    }
    switch (s) {
    case H_MIN:                                 /* (to the dock: V83) */
        ttag = "MIN";
        XCALL(bDock, dock_min, FB)(w);
        return;
    case H_CLOSE:
        ttag = "CLOSE";
        XCALL(bOps, win_close, FB)(w);
        return;
    case H_ZOOM:
        ttag = wins[w].zw ? "UNZOOM" : "ZOOM";
        XCALL(bBars, win_zoom, FB)(w);
        return;
    case H_TITLE:
        drag = D_MOVE;
        offx = x - L(w);
        offy = y - T(w);
        return;
    case H_BACK:
    case H_FWD:
        ttag = "NAV";
        shell_nav(w, s == H_BACK ? -1 : 1);
        return;
    case H_ICONS:
    case H_LIST:
        ttag = s == H_ICONS ? "ICONS" : "LIST";
        shell_setview(s == H_ICONS ? V_ICON : V_LIST);
        return;
    case H_SEARCH:
        shell_search(w);
        return;
    case H_PREV:
    case H_NEXT:
        ttag = "PAGE";
        XCALL(bView, view_page, FBB)(w, s == H_PREV ? -1 : 1);
        return;
    case H_SIZE:
        drag = D_SIZE;
        return;
    case H_BODY:
        break;
    default:
        return;
    }
    if (found_on) {
        UseFont(FONT_UI);
        XCALL(bFiles, found_press, void (*)(unsigned int, unsigned int))(x, y);
        UseSystemFont();
        return;
    }
    s = XCALL(bView, view_hit, unsigned char (*)(unsigned char, unsigned int, unsigned int))(w, x, y);
    if (s == 0xFE) {
        ttag = "HEADER";
        XCALL(bDir, view_header, void (*)(unsigned char, unsigned int))(w, x);
        return;
    }
    if (s >= 0xFA && s <= 0xFD) {               /* the list's scroll bar */
        ttag = s >= 0xFC ? "SCROLL" : "SCROLLPAGE";
        XCALL(bView, view_page, FBB)(w, s == 0xFD ? -2 : s == 0xFC ? 2 : s == 0xFB ? -1 : 1);
        return;
    }
    if (s != 0xFF && s == p->sel && dblClickCount) {
        dblClickCount = 0;
        shell_open(w, p->order[s]);
        return;
    }
    if (!ttag)
        ttag = "SELECT";
    XCALL(bView, view_select, FBB)(w, s);
    if (s != 0xFF) {
        dblClickCount = CLICK_COUNT;
        drag = D_PRESS;
        dent = p->order[s];
    }
}

void win_press(void)
{
    unsigned long c = cycles();
    ttag = 0;
    press_();
    tlog(c);
}
