/* Preferences: Veranda's control panel, a desk accessory in C.

   A row a setting, each changed with its < and > buttons (or a click on
   a switch): the screen's blanking delay (blankDelay), the paper and the
   ink (palette entries 0 and 1 at every program's start: SetColors; shown
   at once in the window), the sounds (on or off, their volume 0-63, the
   menu ticks: DECISIONS V69), the pointer's speed (x1, x2,
   x4), the video mode Veranda starts in and gives programs that keep the
   mode (the shell's), the desktop (Veranda's shell, or TopDesk: its
   TopDesk.cvt, from the applications' folder, copied as DESKTOP.cvt in
   the system folder /VERANDA (DECISIONS V71), or DESKTOP.cvt
   deleted: DECISIONS V46), the time zone (kept for programs that take the
   time from a network), and the date and time (SetClock, the X16's
   battery-backed clock: Set clock).

   Theme (the button by Save) shows the colour theme instead (DECISIONS
   V65, V74, sdk theme.h): a preset (Veranda, Classic, Graphite, Mint, or
   Custom once a colour is changed), the accent (title bars), the
   desktop (the area behind windows: the shell's icons) and the
   highlight (selections, an open menu), each one of Veranda Look 2's
   light colours (dark ones would hide the one ink), with a swatch, the
   mode (light or dark: Look 2's palette swapped, at once on the whole
   screen), and a preview: a small shell in those colours; the window's
   own title bar takes the accent at once. Saved in the settings block's
   bytes 10-14 (14: LOOK_V2 and LOOK_DARK); the shell takes it when the
   accessory closes, and at every start.

   Desktop (the button by Theme) shows the desktop's background (Veranda
   Look 2, step C: DECISIONS V78, sdk wallcfg.h): a picture (a wallpaper
   file, tools/mkwall.py, chosen from /VERANDA/WALLPAPERS with Choose;
   Dusk, the default), the theme's plain desktop colour, or one of the
   shell's eight patterns on it (the classic look); dark mode takes the
   picture's dark variant by itself. Saved in WALLPAPER.CFG (the system's
   data folder) with the rest; the shell takes it when the accessory
   closes.

   Save writes "Veranda Prefs" in the system's data folder (/VERANDA/.SYSTEM;
   a GEOS file of type
   SYSTEM, its data the kernel's settings block: SysPrefs, DECISIONS V56),
   which the kernel reads when Veranda starts, and gives the block to the
   running system (blankDelay, the sounds and the speed at once; the colours
   and the mode from the next program's start). Closing without Save
   leaves everything as it was (the window's colours go with it).

   MIT licence (see LICENSE). */
#include <veranda.h>
#include <vr.h>
#include <string.h>
#include <theme.h>
#include <vrpath.h>
#include <wallcfg.h>
#include "daui.h"

#define W       480
#define LX      16              /* the labels (in the window) */
#define CX      184             /* the controls */
#define VW      128             /* a setting's value */
#define NROW    10

static unsigned char p[PREFS_SIZE];
static unsigned char rh, ry;            /* the rows' height, the first row */
static unsigned char iblank, ipaper, iink, ispeed, imode, idesk, topdesk;
static signed char tz;
static unsigned char dd, dm, dy, th, tmi;       /* the clock being edited */
static char line[64];
static unsigned char page;              /* 0 the settings, 1 the theme, 2 the desktop */
static unsigned char wcfg[WALL_CFGLEN]; /* the desktop's background (wallcfg.h) */
static struct theme tt;                 /* the theme being edited */

/* the colours offered: none and Look 2's light ones (V74) */
#define NCOL 13
static const unsigned char cols[NCOL] = { 0, TITLE, SELECT, SKY2, CYAN, GREEN, YELLOW,
    ORANGE, RED, PURPLE, DUSK, CHROME, SIDEBAR };
static const char *const colt[NCOL] = { "None", "Sky", "Blue", "Azure", "Cyan", "Green", "Yellow",
    "Orange", "Rose", "Lilac", "Dusk", "Chrome", "Mist" };
static const char *const presett[THEME_PRESETS + 1] = { "Veranda", "Classic", "Graphite", "Mint",
    "Custom" };
#define TROWS 5
static const char *const tlabels[TROWS] = { "Theme", "Accent (title bars)", "Desktop", "Highlight", "Mode" };
#define DROWS 4
static const char *const dlabels[DROWS] = { "Background", "Picture", "Pattern", "Colour" };
static const char *const kindt[3] = { "Picture", "Plain colour", "Pattern" };
static const unsigned char pats[8] = { 2, 3, 5, 31, 23, 19, 0, 33 };   /* (the shell's) */

static const unsigned int blanks[8] = { 0, 60, 120, 300, 600, 900, 1800, 3600 };
static const char *const blankt[8] = { "never", "1 minute", "2 minutes", "5 minutes",
    "10 minutes", "15 minutes", "30 minutes", "1 hour" };
static const unsigned int papers[6] = { 0x0FFF, 0x0FFD, 0x0DFE, 0x0DEF, 0x0DDD, 0x0EDB };
static const char *const papert[6] = { "White", "Cream", "Mint", "Sky", "Silver", "Sand" };
static const unsigned int inks[6] = { 0x0223, 0x0000, 0x0026, 0x0420, 0x0030, 0x0303 };
static const char *const inkt[6] = { "Slate", "Black", "Navy", "Brown", "Forest", "Plum" };
static const char *const speedt[3] = { "Normal", "Fast", "Faster" };
static const char *const modet[4] = { "640 x 480", "640 x 240", "320 x 240", "as it is" };
static const char *const deskt[2] = { "Veranda's shell", "TopDesk" };
static const char *const labels[NROW] = { "Blank the screen after", "Paper", "Ink", "Sounds",
    "Pointer speed", "Video mode at start", "Desktop", "Time zone", "Date", "Time" };
static const char *const mon[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug",
    "Sep", "Oct", "Nov", "Dec" };

#ifdef PREFS_TEST
void da_test_start(void);
#endif

static char logl[80];
static void say(const char *s)
{
    *ui_put(ui_put(logl, "PREFS "), s) = 0;
    LogText(logl);
}

static unsigned int ry_of(unsigned char r)
{
    return ui_y0 + ry + r * rh;
}

/* a spinner: < at x, the value in w, > after it */
static void spin(unsigned int x, unsigned int y, unsigned int w, const char *v)
{
    ui_button(x, y, 16, "<", C_BTN);
    ui_clear(x + 16, y, w, UI_BH);
    ui_color(x + 16, y, w, UI_BH, C_NONE);
    ui_textc(v, x + 16, w, y + 11);
    ui_button(x + 16 + w, y, 16, ">", C_BTN);
}

/* the sounds' volume, 0-63: PREFS_VOLSET + 63 - it; without PREFS_VOLSET
   (a file saved before DECISIONS V72) full, as the kernel reads it */
static unsigned char volume(void)
{
    return p[PREFS_VOLUME] & PREFS_VOLSET ? 63 - (p[PREFS_VOLUME] & 63) : 63;
}

static unsigned char mdays(unsigned char y, unsigned char m)
{
    static const unsigned char n[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return m == 2 && !(y & 3) ? 29 : n[m - 1];
}

static void draw_row(unsigned char r)
{
    unsigned int x = ui_x0 + CX, y = ry_of(r);
    char *q;
    switch (r) {
    case 0: spin(x, y, VW, blankt[iblank]); break;
    case 1: spin(x, y, VW, papert[ipaper]); break;
    case 2: spin(x, y, VW, inkt[iink]); break;
    case 3:                             /* on/off, < volume >, the menu ticks */
        ui_button(x, y, 48, p[6] & PREFS_NOSOUND ? "Off" : "On", p[6] & PREFS_NOSOUND ? C_BTN : C_GREEN);
        ui_num(line, volume(), 0);
        spin(x + 56, y, 32, line);
        ui_button(x + 128, y, 104, p[6] & PREFS_MENUTICK ? "Menu ticks" : "No menu ticks",
                  p[6] & PREFS_MENUTICK ? C_GREEN : C_BTN);
        break;
    case 4: spin(x, y, VW, speedt[ispeed]); break;
    case 5: spin(x, y, VW, modet[imode]); break;
    case 6: spin(x, y, VW, topdesk || idesk ? deskt[idesk] : "shell (no TopDesk)"); break;
    case 7:
        q = ui_put(line, "UTC");
        if (tz) {
            unsigned char a = tz < 0 ? -tz : tz;
            *q++ = tz < 0 ? '-' : '+';
            q = ui_num(q, a / 4, 0);
            *q++ = ':';
            ui_num(q, a % 4 * 15, 2);
        }
        spin(x, y, VW, line);
        break;
    case 8:
        ui_num(line, dd, 0);
        spin(x, y, 32, line);
        spin(x + 72, y, 32, mon[dm - 1]);
        ui_num(line, 2000 + dy, 0);
        spin(x + 144, y, 40, line);
        break;
    case 9:
        ui_num(line, th, 2);
        spin(x, y, 32, line);
        ui_num(line, tmi, 2);
        spin(x + 72, y, 32, line);
        ui_button(x + 144, y, 72, "Set clock", C_BTN);
        break;
    }
}

static unsigned int bx(unsigned char i)
{
    return ui_x0 + W - 8 - (2 - i) * 88;
}

static unsigned char *colp(unsigned char r)
{
    return r == 1 ? &tt.accent : r == 2 ? &tt.desk : &tt.hilite;
}

static unsigned char coli(unsigned char c)
{
    unsigned char i;
    for (i = 0; i < NCOL; ++i)
        if (cols[i] == c)
            return i;
    return 0;
}

/* a preview: a small shell in the theme's colours (card aligned): the
   menu bar on the chrome (with a highlight: V74), a title bar on the
   accent, the desktop, three icons, one selected */
static void preview_theme(void)
{
    unsigned int x = ui_x0 + LX, y = ry_of(TROWS), w = W - 2 * LX, h = ry_of(NROW - 1) + UI_BH - y;
    unsigned int cy = y + 32, ch = h - 40;
    unsigned char i;
    static const char *const names[3] = { "Docs", "Paint", "Notes" };
    ui_clear(x, y, w, h);
    ui_color(x, y, w, h, tt.desk);
    ui_color(x, y, w, 16, tt.hilite ? CHROME : 0);
    ui_color(x, y + 16, w, 16, tt.accent);
    ui_frame(x, y, w, h);
    ui_text("Veranda   File   View", x + 8, y + 11);
    WHorizontalLine(0xFF, x, x + w - 1, y + 15);
    ui_text("/VERANDA", x + 8, y + 27);
    WHorizontalLine(0xFF, x, x + w - 1, y + 31);
    if (ch > 40)
        ch = 40;
    for (i = 0; i < 3; ++i) {
        unsigned int cx = x + 8 + i * 96;
        ui_frame(cx + 32, cy + 4, 16, 18);
        ui_textc(names[i], cx, 80, cy + 33);
        if (i == 1) {
            if (tt.hilite)
                ui_color(cx, cy, 80, ch, tt.hilite);
            else
                WInvertRectangle(cx + 2, cy, cx + 77, cy + ch - 1);
        }
    }
    /* the window's own title bar: the accent at once */
    ui_color(ui_x0, ui_y0, W, UI_BAR, tt.accent ? tt.accent : C_BLUE);
}

static void draw_trow(unsigned char r)
{
    unsigned int x = ui_x0 + CX, y = ry_of(r);
    if (r == 0) {
        spin(x, y, VW, presett[tt.preset]);
        return;
    }
    if (r == TROWS - 1) {
        spin(x, y, VW, tt.dark ? "Dark" : "Light");
        return;
    }
    spin(x, y, VW, colt[coli(*colp(r))]);
    ui_frame(x + VW + 40, y, 32, UI_BH);
    ui_color(x + VW + 40, y, 32, UI_BH, *colp(r));
}

#define DBX 120                 /* the Desktop button */

/* the desktop page's row r (V78) */
static void draw_drow(unsigned char r)
{
    unsigned int x = ui_x0 + CX, y = ry_of(r);
    unsigned char k = wcfg[WALL_KIND];
    switch (r) {
    case 0:
        spin(x, y, VW, kindt[k < 3 ? k : 0]);
        break;
    case 1:
        ui_button(x, y, 64, "Choose", k == WALL_PICTURE ? C_GREEN : C_BTN);
        ui_clear(x + 72, y, 176, UI_BH);
        ui_color(x + 72, y, 176, UI_BH, C_NONE);
        ui_text((char *)wcfg + WALL_NAME, x + 72, y + 11);
        break;
    case 2:
        *ui_num(ui_put(line, "Pattern "), wcfg[WALL_PAT] + 1, 0) = 0;
        spin(x, y, VW, line);
        ui_frame(x + VW + 40, y, 32, UI_BH);
        SetPattern(pats[wcfg[WALL_PAT] & 7]);
        WRectangle(x + VW + 41, y + 1, x + VW + 70, y + UI_BH - 2);
        break;
    case 3:
        spin(x, y, VW, colt[coli(tt.desk)]);
        ui_frame(x + VW + 40, y, 32, UI_BH);
        ui_color(x + VW + 40, y, 32, UI_BH, tt.desk);
        break;
    }
}

/* the rows' area: the page's labels and controls */
static void draw_page(void)
{
    unsigned char r;
    unsigned int y = ry_of(0);
    ui_clear(ui_x0 + 8, y, W - 16, ry_of(NROW - 1) + UI_BH + 1 - y);
    ui_color(ui_x0 + 8, y, W - 16, ry_of(NROW - 1) + UI_BH - y, C_NONE);
    if (page == 2) {
        for (r = 0; r < DROWS; ++r) {
            ui_text(dlabels[r], ui_x0 + LX, ry_of(r) + 11);
            draw_drow(r);
        }
        ui_text("Dark mode shows the picture's dark variant.", ui_x0 + LX, ry_of(DROWS) + 11);
        ui_text("Pictures: " VR_WALLDIR " (tools/mkwall.py).", ui_x0 + LX, ry_of(DROWS + 1) + 11);
    } else if (page) {
        for (r = 0; r < TROWS; ++r) {
            ui_text(tlabels[r], ui_x0 + LX, ry_of(r) + 11);
            draw_trow(r);
        }
        preview_theme();
    } else {
        for (r = 0; r < NROW; ++r) {
            ui_text(labels[r], ui_x0 + LX, ry_of(r) + 11);
            draw_row(r);
        }
    }
    ui_button(ui_x0 + 208, ui_y0 + ui_h - 24, 80, page ? "Settings" : "Theme", C_BTN);
    ui_button(ui_x0 + DBX, ui_y0 + ui_h - 24, 80, "Desktop", page == 2 ? C_SEL : C_BTN);
}

static void tchange(unsigned char r, signed char d)
{
    unsigned char *c;
    if (r == 0) {
        tt.preset = tt.preset >= THEME_PRESETS ? (d > 0 ? 0 : THEME_PRESETS - 1)
            : (tt.preset + THEME_PRESETS + d) % THEME_PRESETS;
        theme_preset(&tt);
        say(presett[tt.preset]);
        draw_page();
        return;
    }
    if (r == TROWS - 1) {               /* light, dark: the palette at once */
        tt.dark ^= LOOK_DARK;
        theme.dark = tt.dark;
        theme.paper = papers[ipaper];
        theme.ink = inks[iink];
        theme_palette();
        say(tt.dark ? "DARK" : "LIGHT");
        draw_trow(r);
        return;
    }
    c = colp(r);
    *c = cols[(coli(*c) + NCOL + d) % NCOL];
    tt.preset = THEME_CUSTOM;
    draw_trow(0);
    draw_trow(r);
    preview_theme();
}

static void draw(void)
{
    ui_window("Preferences", W, ui_h);
    draw_page();
    WHorizontalLine(0xFF, ui_x0 + 8, ui_x0 + W - 9, ui_y0 + ui_h - 32);
    ui_button(bx(0), ui_y0 + ui_h - 24, 80, "Save", C_GREEN);
    ui_button(bx(1), ui_y0 + ui_h - 24, 80, "Close", C_BTN);
}

static unsigned char find(const unsigned int *t, unsigned char n, unsigned int v)
{
    unsigned char i;
    for (i = 0; i < n; ++i)
        if (t[i] == v)
            return i;
    return 0;
}

static void preview(void)
{
    if (!tt.dark)               /* (dark mode has its own paper and ink) */
        SetColors(papers[ipaper], inks[iink]);
}

/* the settings into p */
static void collect(void)
{
    p[0] = papers[ipaper] & 255;
    p[1] = papers[ipaper] >> 8;
    p[2] = inks[iink] & 255;
    p[3] = inks[iink] >> 8;
    p[4] = blanks[iblank] & 255;
    p[5] = blanks[iblank] >> 8;
    p[6] = (p[6] & (PREFS_NOSOUND | PREFS_MENUTICK)) | ispeed;
    p[7] = imode < 3 ? imode : 0xFF;
    p[8] = tz;
    p[9] = idesk;
    p[PREFS_ACCENT] = tt.accent;
    p[PREFS_DESK] = tt.desk;
    p[PREFS_HILITE] = tt.hilite;
    p[PREFS_THEME] = THEME_SET | tt.preset;
    p[PREFS_LOOK] = LOOK_V2 | tt.dark;  /* (V74: Look 2's colours) */
}

/* from (in the applications' folder) copied to "to" in the system folder
   (an open stream keeps its folder) */
static unsigned char copy_file(const char *from, const char *to)
{
    static unsigned char buf[512];
    unsigned char a, b, e = 0;
    unsigned int n;
    vr_cd(VR_APPS);
    a = FOpen((void *)from, 'r');
    if (a < 2 || a > 9 || dosStatus >= 20)
        return FILE_NOT_FOUND;
    vr_cd(VR_SYS);
    b = FOpen((void *)to, 'w');
    if (b < 2 || b > 9 || dosStatus >= 20) {
        FClose(a);
        return DOS_ERROR;
    }
    while ((n = FRead(a, buf, sizeof buf)) != 0 && !e)
        e = FWrite(b, buf, n);
    FClose(a);
    if (FClose(b) && !e)
        e = DOS_ERROR;
    return e;
}

static void desktop(void)
{
    unsigned char e = 0;
    if (idesk) {
        e = copy_file("TopDesk.cvt", "DESKTOP.cvt");
        say(e ? "DESKTOP NOT COPIED" : "DESKTOP TOPDESK");
    } else if ((vr_cd(VR_SYS), FindFile("DESKTOP")) == 0) {
        e = FDelete("DESKTOP.cvt");
        say("DESKTOP SHELL");
    }
    sys_leave();
    if (e)
        ui_msg("The desktop could not be changed", "in the system folder.");
}

/* WALLPAPER.CFG (V78) into wcfg; without one: the picture Dusk */
static void wall_read(void)
{
    unsigned char h;
    memset(wcfg, 0, sizeof wcfg);
    wcfg[0] = 'W';
    wcfg[1] = 'P';
    wcfg[2] = 1;
    strcpy((char *)wcfg + WALL_NAME, WALL_DEFAULT);
    strcpy((char *)wcfg + WALL_DIR, VR_WALLDIR);
    sys_enter();
    h = FOpen(WALL_CFG, 'r');
    if (h >= 2 && h <= 9) {
        if (FRead(h, line, WALL_CFGLEN) == WALL_CFGLEN && line[0] == 'W' && line[1] == 'P' && line[2] == 1)
            memcpy(wcfg, line, WALL_CFGLEN);
        FClose(h);
    }
    wcfg[WALL_NAME + WALL_NAMELEN - 1] = 0;
    wcfg[WALL_DIR + WALL_DIRLEN - 1] = 0;
    if (wcfg[WALL_KIND] > WALL_PATTERN)
        wcfg[WALL_KIND] = WALL_PICTURE;
    wcfg[WALL_PAT] &= 7;
    sys_leave();
}

static unsigned char wall_save(void)
{
    unsigned char h, e = DOS_ERROR;
    char *q;
    sys_enter();
    h = FOpen(WALL_CFG, 'w');
    if (h >= 2 && h <= 9) {
        e = FWrite(h, wcfg, WALL_CFGLEN);
        if (FClose(h) && !e)
            e = DOS_ERROR;
    }
    sys_leave();
    q = ui_put(line, "DESKTOP ");
    q = ui_put(q, kindt[wcfg[WALL_KIND]]);
    *q++ = ' ';
    q = ui_put(q, (char *)wcfg + WALL_NAME);
    *q++ = ' ';
    *ui_num(q, wcfg[WALL_PAT], 0) = 0;
    say(line);
    return e;
}

/* Choose: a wallpaper of /VERANDA/WALLPAPERS (DBGETFILES: the GEOS
   documents of class Wallpaper) */
struct box4 {
    unsigned char pos;
    unsigned char t1, x1, y1;
    unsigned char t2, x2, y2;
    unsigned char t3, x3, y3;
    unsigned char end;
};
static const struct box4 wallBox = {
    DEF_DB_POS | 1,
    DBGETFILES, 4, 4,
    OPEN, DBI_X_2, DBI_Y_0,
    CANCEL, DBI_X_2, DBI_Y_2,
    0
};

static void choose(void)
{
    static char name[20];
    unsigned char r;
    if (ChDir(VR_WALLDIR)) {
        ui_msg("There is no folder", VR_WALLDIR ".");
        return;
    }
    name[0] = 0;
    r5 = (unsigned int)name;
    r7L = APPL_DATA;
    r10 = (unsigned int)WALL_CLASS;
    r = DoDlgBox((void *)&wallBox);
    sys_leave();
    if (r == DBGETFILES) {
        ui_msg("There are no pictures in", VR_WALLDIR ".");
        return;
    }
    if (r != OPEN || !name[0])
        return;
    strcpy((char *)wcfg + WALL_NAME, name);
    strcpy((char *)wcfg + WALL_DIR, VR_WALLDIR);
    wcfg[WALL_KIND] = WALL_PICTURE;
    *ui_put(ui_put(line, "PICTURE "), name) = 0;
    say(line);
    draw_page();
}

static void save(void)
{
    unsigned char e, i;
    char *q;
    collect();
    e = sys_write("Veranda Prefs", "Veranda Prefs V1.0", "Veranda's settings (Preferences).", p, PREFS_SIZE);
    SysPrefs(PREFS_SET, p);
    desktop();
    e |= wall_save();
    if (e) {
        ui_msg("The settings could not be written", "in the system folder.");
        return;
    }
    q = ui_put(line, "SAVED");
    for (i = 0; i < PREFS_SIZE; ++i) {
        *q++ = ' ';
        q = ui_num(q, p[i], 0);
    }
    say(line);
    ui_clear(ui_x0 + LX, ui_y0 + ui_h - 24, DBX - LX - 8, 16);
    ui_text("Saved.", ui_x0 + LX, ui_y0 + ui_h - 13);
}

static void set_clock(void)
{
    unsigned int Y = 2000 + dy - (dm < 3);
    static const unsigned char t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    unsigned char w = (Y + Y / 4 - Y / 100 + Y / 400 + t[dm - 1] + dd) % 7;   /* 0 Sunday */
    SetClock(100 + dy, dm, dd, th, tmi, 0, w + 1);
    say("CLOCK SET");
}

/* row r's < (d = -1) or > (d = 1); for the date and time k: the part */
static void change(unsigned char r, unsigned char k, signed char d)
{
    switch (r) {
    case 0: iblank = (iblank + 8 + d) % 8; break;
    case 1: ipaper = (ipaper + 6 + d) % 6; preview(); break;
    case 2: iink = (iink + 6 + d) % 6; preview(); break;
    case 3:
        if (k == 0)
            p[6] ^= PREFS_NOSOUND;
        else if (k == 2)
            p[6] ^= PREFS_MENUTICK;
        else {                          /* the volume in steps of 9 */
            k = volume();
            k = d > 0 ? (k > 54 ? 63 : k + 9) : (k < 9 ? 0 : k - 9);
            p[PREFS_VOLUME] = PREFS_VOLSET | (63 - k);
        }
        break;
    case 4: ispeed = (ispeed + 3 + d) % 3; break;
    case 5: imode = (imode + 4 + d) % 4; break;
    case 6: if (topdesk || idesk) idesk ^= 1; break;
    case 7:
        if ((d > 0 && tz < 56) || (d < 0 && tz > -48))
            tz += d;
        break;
    case 8:
        if (k == 0)
            dd = dd + d < 1 ? mdays(dy, dm) : dd + d > mdays(dy, dm) ? 1 : dd + d;
        else if (k == 1)
            dm = (dm + 11 + d) % 12 + 1;
        else if ((d > 0 && dy < 99) || (d < 0 && dy))
            dy += d;
        if (dd > mdays(dy, dm))
            dd = mdays(dy, dm);
        break;
    case 9:
        if (k == 0)
            th = (th + 24 + d) % 24;
        else
            tmi = (tmi + 60 + d) % 60;
        break;
    }
    draw_row(r);
}

static void close_box(void)
{
    say("CLOSED");
    RstrAppl();
}

/* otherPressVec */
static void press(void)
{
    unsigned char r, k;
    unsigned int x, y, w;
    if (!ui_press())
        return;
    if (ui_closebox()) {
        close_box();
        return;
    }
    if (ui_in(bx(0), ui_y0 + ui_h - 24, 80, UI_BH)) {
        save();
        return;
    }
    if (ui_in(bx(1), ui_y0 + ui_h - 24, 80, UI_BH)) {
        close_box();
        return;
    }
    if (ui_in(ui_x0 + 208, ui_y0 + ui_h - 24, 80, UI_BH)) {
        page = page ? 0 : 1;
        say(page ? "THEME PAGE" : "SETTINGS PAGE");
        draw_page();
        return;
    }
    if (ui_in(ui_x0 + DBX, ui_y0 + ui_h - 24, 80, UI_BH)) {
        page = 2;
        say("DESKTOP PAGE");
        draw_page();
        return;
    }
    if (page == 2) {
        for (r = 0; r < DROWS; ++r) {
            y = ry_of(r);
            x = ui_x0 + CX;
            if (ui_py < y || ui_py >= y + UI_BH)
                continue;
            if (r == 1) {
                if (ui_in(x, y, 64, UI_BH))
                    choose();
                return;
            }
            k = ui_in(x, y, 16, UI_BH) ? 0xFF : ui_in(x + 16 + VW, y, 16, UI_BH) ? 1 : 0;
            if (!k)
                return;
            if (r == 0)
                wcfg[WALL_KIND] = (wcfg[WALL_KIND] + 3 + (signed char)k) % 3;
            else if (r == 2) {
                wcfg[WALL_PAT] = (wcfg[WALL_PAT] + 8 + (signed char)k) & 7;
                wcfg[WALL_KIND] = WALL_PATTERN;
            } else {
                tt.desk = cols[(coli(tt.desk) + NCOL + (signed char)k) % NCOL];
                tt.preset = THEME_CUSTOM;
            }
            say(kindt[wcfg[WALL_KIND]]);
            draw_page();
            return;
        }
        return;
    }
    if (page) {
        for (r = 0; r < TROWS; ++r) {
            y = ry_of(r);
            x = ui_x0 + CX;
            if (ui_py < y || ui_py >= y + UI_BH)
                continue;
            if (ui_in(x, y, 16, UI_BH))
                tchange(r, -1);
            else if (ui_in(x + 16 + VW, y, 16, UI_BH))
                tchange(r, 1);
            return;
        }
        return;
    }
    for (r = 0; r < NROW; ++r) {
        y = ry_of(r);
        if (ui_py < y || ui_py >= y + UI_BH)
            continue;
        x = ui_x0 + CX;
        if (r == 3) {
            if (ui_in(x, y, 48, UI_BH))
                change(r, 0, 1);
            else if (ui_in(x + 56, y, 16, UI_BH))
                change(r, 1, -1);
            else if (ui_in(x + 104, y, 16, UI_BH))
                change(r, 1, 1);
            else if (ui_in(x + 128, y, 104, UI_BH))
                change(r, 2, 1);
            return;
        }
        if (r == 9 && ui_in(x + 144, y, 72, UI_BH)) {
            set_clock();
            return;
        }
        for (k = 0; k < (r >= 8 ? (r == 8 ? 3 : 2) : 1); ++k) {
            w = r < 8 ? VW : k == 2 ? 40 : 32;
            if (ui_in(x, y, 16, UI_BH)) {
                change(r, k, -1);
                return;
            }
            if (ui_in(x + 16 + w, y, 16, UI_BH)) {
                change(r, k, 1);
                return;
            }
            x += 72;
        }
        return;
    }
}

static void typed(void)
{
    if (keyData == 27)
        close_box();
    else if (keyData == 13)
        save();
}

int main(void)
{
    if (screenWidth < 640) {
        ui_msg("Preferences needs a screen", "640 wide.");
        RstrAppl();
    }
    ui_h = screenHeight >= 400 ? 400 : 232;
    rh = ui_h >= 400 ? 32 : 16;
    ry = ui_h >= 400 ? 32 : 24;
    ui_x0 = ((screenWidth - W) / 2) & ~7;
    ui_y0 = ((screenHeight - ui_h) / 2) & ~7;
    SysPrefs(PREFS_GET, p);
    iblank = find(blanks, 8, p[4] | p[5] << 8);
    ipaper = find(papers, 6, p[0] | p[1] << 8);
    iink = find(inks, 6, p[2] | p[3] << 8);
    ispeed = p[6] & 3;
    imode = p[7] < 3 ? p[7] : 3;
    tz = p[8];
    theme_load();
    tt.accent = theme.accent;
    tt.desk = theme.desk;
    tt.hilite = theme.hilite;
    tt.preset = theme.preset > THEME_CUSTOM ? THEME_CUSTOM : theme.preset;
    tt.dark = theme.dark;
    vr_cd(VR_APPS);                     /* TopDesk with the applications, */
    topdesk = FindFile("TopDesk") == 0;
    vr_cd(VR_SYS);                      /* DESKTOP.cvt by the shell (V46, V71) */
    idesk = FindFile("DESKTOP") == 0;
    sys_leave();
    wall_read();
    dd = day;
    dm = month;
    dy = year;
    th = hour;
    tmi = minutes;
    if (!dm || dm > 12)
        dm = 1;
    if (!dd || dd > mdays(dy, dm))
        dd = 1;
    draw();
    otherPressVec = VR_FN(press);
    keyVector = VR_FN(typed);
    say("RUNNING");
#ifdef PREFS_TEST
    da_test_start();
#endif
    return 0;
}
