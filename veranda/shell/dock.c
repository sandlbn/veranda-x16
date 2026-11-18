/* Veranda shell: the dock (overlay 10, with the tiles: a bank of its own
   would cost a desk accessory its room at 512 KB; Veranda Look 2, step E, DECISIONS
   V83).

   A strip at the bottom of the desktop, over its picture and the windows,
   centred: card rows SH / 8 - 8 to SH / 8 - 3 (52-57 at 480
   lines), the sidebar's colour in a rounded frame, an item every 5 cards:
   the pinned applications (their tiles), the applications put aside that
   are not pinned (the switcher, TaskCtl: their own tiles, V84, from the
   path the shell gives each program it starts as its note; a document
   tile when it has none), the windows
   minimised (a folder tile); a dot under each application that is aside.

   A click: a pinned application starts, or comes back if it is aside
   (TaskCtl TASK_RESUME: the shell ends, the application goes on where it
   was); a minimised window opens again. A right click: a small menu over
   the item, Quit (an application aside: dropped, after a question) and
   Unpin, or Restore for a window; the next press chooses (anywhere else:
   nothing). The minimised windows are kept in the desk file (SHELL.DSK
   version 3, byte 15: dockMin), as the shell starts again after every
   program. Console: SHELL DOCK <items>, SHELL DOCK RESUME/QUIT/UNPIN/
   MIN/RESTORE ... */
#include <veranda.h>
#include <string.h>
#include <theme.h>
#include "shell.h"
#include "winp.h"
#include "tiles.h"
#pragma code-name ("OVERLAY10")
#pragma rodata-name ("OVL10RO")
#pragma bss-name ("OVL10BSS")

#define DOCKMAX (MAXPINS + TASK_MAX + NWIN)
enum { D_PIN, D_TASK, D_WIN };

static unsigned char n, x0, top, tasks;
static unsigned char kind[DOCKMAX], idx[DOCKMAX], slot[DOCKMAX];
static unsigned char ttile[TASK_MAX];                   /* the tasks' tiles (dir.c: the desk bank's TT_AT) */
static char tname[TASK_MAX][17];
static unsigned char pop = 0xFF, popN, popX, popY;     /* the menu: its item, its lines, card, row */
static unsigned char popAct[2];
enum { P_QUIT, P_UNPIN, P_RESTORE };
static const char *const popText[3] = { "Quit", "Unpin", "Restore" };

static void say(const char *what, const char *name)
{
    strcpy(msg, "SHELL DOCK ");
    strcat(msg, what);
    strcat(msg, name);
    LogText(msg);
}

/* a pin's name without ".cvt" to str */
static void pinName(unsigned char p)
{
    strcpy(str, pins[p].name);
    if (shell_is_cvt(str))
        str[strlen(str) - 4] = 0;
}

/* item i's name to str */
static void itemName(unsigned char i)
{
    if (kind[i] == D_PIN)
        pinName(idx[i]);
    else if (kind[i] == D_TASK)
        strcpy(str, tname[idx[i]]);
    else
        strcpy(str, wins[idx[i]].path);
}

/* the items: the pins, the tasks (a pin's when its first 16 characters
   are the task's name), the minimised windows */
void dock_layout(void)
{
    unsigned char i, s, max = (SW / 8 - 1) / 5;
    n = 0;
    tasks = 0;
    top = SH / 8 - 8;
    bget(bDesk, TT_AT, pic, TASK_MAX);     /* (through main RAM: bget maps the bank at $A000) */
    memcpy(ttile, pic, TASK_MAX);
    for (i = 0; i < npins && n < max; ++i) {
        kind[n] = D_PIN;
        idx[n] = i;
        slot[n++] = 0xFF;
    }
    for (s = 0; s < TASK_MAX; ++s) {
        TaskCtl(TASK_INFO, s, buf);
        tname[s][0] = 0;
        if (!buf[TR_STATE])
            continue;
        ++tasks;
        memcpy(tname[s], buf + TR_NAME, 17);
        for (i = 0; i < n; ++i) {
            if (kind[i] != D_PIN)
                continue;
            pinName(idx[i]);
            str[16] = 0;
            if (!strcmp(str, tname[s]))
                break;
        }
        if (i < n) {
            slot[i] = s;
        } else if (n < max) {
            kind[n] = D_TASK;
            idx[n] = s;
            slot[n++] = s;
        }
    }
    for (i = 0; i < NWIN && n < max; ++i)
        if ((dockMin >> i & 1) && !wins[i].open) {
            kind[n] = D_WIN;
            idx[n] = i;
            slot[n++] = 0xFF;
        }
    x0 = (SW / 8 - (n * 5 + 1)) / 2;
    if (!testing)
        return;
    strcpy(msg, "SHELL DOCK");
    for (i = 0; i < n; ++i) {
        itemName(i);
        strcat(msg, i ? ", " : " ");
        strcat(msg, str);
        if (slot[i] != 0xFF)
            strcat(msg, " *");
    }
    LogText(msg);
}

static unsigned int L0(void) { return x0 * 8; }
static unsigned int R0(void) { return (x0 + n * 5 + 1) * 8 - 1; }
static unsigned int T0(void) { return top * 8; }
static unsigned int B0(void) { return (top + 6) * 8 - 1; }

static unsigned char tileOf(unsigned char i)
{
    if (kind[i] == D_PIN)
        return pinTile[idx[i]];
    if (kind[i] == D_TASK && ttile[idx[i]])
        return ttile[idx[i]];
    return TL_BUILT + (kind[i] == D_WIN ? T_FOLDER : T_DOCUMENT);
}

/* the dock where it meets l, t, r, b (the scene: the kernel's clip is on) */
void dock_draw(unsigned int l, unsigned int t, unsigned int r, unsigned int b)
{
    unsigned char i, cx;
    unsigned int y = (top + 5) * 8 + 3;
    if (!n || l > R0() || r < L0() || t > B0() || b + (pop != 0xFF ? 48 : 0) < T0())
        return;
    SetPattern(0);
    WRectangle(L0(), T0(), R0(), B0());
    shell_rframe(L0(), T0(), R0(), B0(), 6);
    for (i = 0; i < n; ++i) {
        cx = x0 + 1 + i * 5;
        XCALL(bTile, tile_draw, unsigned char (*)(unsigned char, unsigned int, unsigned char, unsigned char, unsigned char))
            (cx, T0() + 8, 2, tileOf(i), kind[i] == D_TASK ? MAXPINS + idx[i] : idx[i]);
        if (slot[i] != 0xFF) {
            SetPattern(1);
            WRectangle(cx * 8 + 15, y, cx * 8 + 17, y + 2);
        }
    }
    if (pop != 0xFF) {                          /* the right click's menu */
        SetPattern(0);
        WRectangle(popX * 8, popY * 8, popX * 8 + 63, (popY + popN * 2) * 8 - 1);
        WFrameRectangle(0xFF, popX * 8, popY * 8, popX * 8 + 63, (popY + popN * 2) * 8 - 1);
        UseFont(FONT_UI);
        for (i = 0; i < popN; ++i) {
            strcpy(str, popText[popAct[i]]);    /* (below $A000 for the kernel) */
            WPutString(str, popX * 8 + 8, (popY + i * 2) * 8 + 3 + baselineOffset);
        }
        UseSystemFont();
    }
}

/* its colours (win_colours: the colour clip) */
void dock_colours(void)
{
    unsigned char i;
    if (!n)
        return;
    look_rect(x0, n * 5 + 1, top, 6, SIDEBAR);
    for (i = 0; i < n; ++i)
        look_rect(x0 + 1 + i * 5, 4, top + 1, 4,
                  XCALL(bTile, tile_colour, unsigned char (*)(unsigned char, unsigned char))(2, tileOf(i)));
    if (pop != 0xFF)
        look_rect(popX, 8, popY, popN * 2, CHROME);
}

/* the dock (and its menu) drawn again, after a change of its items */
static void again(void)
{
    unsigned int l = L0(), r = R0();
    unsigned char was = n;
    dock_layout();
    if (!was) {
        l = L0();
        r = R0();
    } else if (n) {
        if (L0() < l)
            l = L0();
        if (R0() > r)
            r = R0();
    }
    if (!was && !n)
        return;
    if (r < (x0 + 1 + (n ? n - 1 : 0) * 5) * 8 + 63)  /* (the menu, over the last item) */
        r = (x0 + 1 + (n ? n - 1 : 0) * 5) * 8 + 63;
    XCALL(bWin, win_repaint, void (*)(unsigned int, unsigned int, unsigned int, unsigned int))(l, T0() - 48, r, B0());
}

/* the program tmp (its GEOS name) about to start from folder dir: its
   path, dir/tmp, the switcher's note for it (V84: the dock's tile when it
   is put aside); none when it does not fit */
void dock_note(const char *dir)
{
    unsigned char l = strlen(dir);
    msg[0] = 0;
    if (l + strlen(tmp) + 2 <= TASK_NOTE_LEN) {
        strcpy(msg, dir);
        if (l > 1)
            strcat(msg, "/");
        strcat(msg, tmp);
    }
    TaskCtl(TASK_NOTE, 0, msg);
}

/* the shell starts: the dock laid out; a start that failed for want of
   banks said (the switcher's summary: the desktop came instead) */
void dock_init(void)
{
    TaskCtl(TASK_INFO, 0xFF, buf);
    if (buf[TS_ERR] == NO_BANKS) {
        strcpy(msg, "Not enough memory for ");
        strcat(msg, (char *)buf + TS_NAME);
        LogText("SHELL DOCK NO BANKS");
        strcpy(tmp, "Quit a program in the dock first.");
        shell_yes(msg, tmp);
    }
}

/* window w minimised: closed, kept in the dock */
void dock_min(unsigned char w)
{
    dockMin |= 1 << w;
    deskDirty = 1;
    XCALL(bOps, win_close, FB)(w);
    say("MIN ", wins[w].path);
    again();
}

static void act(unsigned char i)
{
    unsigned char r;
    itemName(i);
    if (kind[i] == D_WIN) {
        dockMin &= ~(1 << idx[i]);
        deskDirty = 1;
        say("RESTORE ", str);
        dock_layout();
        XCALL(bOps, win_open, FB)(idx[i]);
        XCALL(bOps, win_front, FB)(idx[i]);
        again();
        return;
    }
    if (slot[i] == 0xFF) {
        say("START ", str);
        shell_pin_start(idx[i]);
        return;
    }
    say("RESUME ", str);
    shell_keep();
    mouseSource = 0;                    /* (the player is overwritten) */
    r = TaskCtl(TASK_RESUME, slot[i], 0);
    shell_err("Cannot bring it back:", r);
}

static void choose(unsigned char a, unsigned char i)
{
    itemName(i);
    if (a == P_RESTORE) {
        act(i);
    } else if (a == P_UNPIN) {
        say("UNPIN ", str);
        XCALL(bDir, pin_remove, FB)(idx[i]);
        again();
    } else {
        strcpy(tmp, "Quit ");
        strcat(tmp, str);
        strcat(tmp, "?");
        strcpy(msg, "What it did not save is lost.");   /* (below $A000) */
        if (shell_yes(tmp, msg)) {
            say("QUIT ", str);
            TaskCtl(TASK_END, slot[i], 0);
            again();
        }
    }
}

/* a press at x, y (w: the window there, $FF none): 1 if the dock took it */
unsigned char dock_press(unsigned int x, unsigned int y, unsigned char w)
{
    unsigned char i, a;
    if (pop != 0xFF) {                          /* the menu's choice, or none */
        i = pop;
        a = 0xFF;
        if (x >= popX * 8 && x < popX * 8 + 64 && y >= popY * 8 && y < (popY + popN * 2) * 8)
            a = popAct[(y / 8 - popY) / 2];
        pop = 0xFF;
        again();
        if (a != 0xFF)
            choose(a, i);
        return 1;
    }
    if (w != 0xFF || !n || x < L0() || x > R0() || y < T0() || y > B0())
        return 0;
    if (x < L0() + 8 || (x - L0() - 8) % 40 >= 32)
        return 1;
    i = (x - L0() - 8) / 40;
    if (i >= n)
        return 1;
    if (!(mouseButtons & 2)) {
        act(i);
        return 1;
    }
    popN = 0;                                   /* the right button: the menu */
    if (kind[i] == D_WIN)
        popAct[popN++] = P_RESTORE;
    if (slot[i] != 0xFF)
        popAct[popN++] = P_QUIT;
    if (kind[i] == D_PIN)
        popAct[popN++] = P_UNPIN;
    popX = x0 + 1 + i * 5;
    popY = top - popN * 2;
    pop = i;
    itemName(i);
    say("MENU ", str);
    again();
    return 1;
}
