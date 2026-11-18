/* Veranda shell: the desktop's picture (overlay 11; Veranda Look 2,
   step C: DECISIONS V78).

   The desktop is a picture (a wallpaper file, tools/mkwall.py), the
   theme's plain desktop colour, or a GEOS pattern on it (the classic
   look), as WALL_CFG says (sdk wallcfg.h: Preferences' Desktop page, the
   shell's options > desktop pattern). A wallpaper is a 1 bpp bitmap (the
   ink a Bayer dither) over a paper colour a card, with palette entries
   12-15 of its own, in a light and a dark variant (dark mode's ink is
   light): the shell reads the variant of the mode into RAM, its bitmap
   (38,400 bytes) into five banks of its own (wallBank: 102 rows a bank,
   so no row crosses one), its 4,800 colours into this overlay's own bank
   (wcol), its four entries into wallPal, and draws from there whatever
   part of the desktop is drawn again (win.c: deskFill, when a window
   moves or closes, the whole screen): the bitmap through VERA's two data
   ports to the screens dispBufferOn selects (wallblit.s, resident: it
   maps the banks; the back screen holds the picture too, so menus and
   dialog boxes recover from it), the colours a card row at a time
   (ColorRect COLOR_BUF) for the cards drawn (wall_fill marks them,
   wall_flush from win_colours writes them, before the icons' and the
   windows' colours). A window's shadow on the picture adds a 50% ink
   dither to it (wall_shade). The file is read again only when the
   picture or the mode (light or dark) changes; at
   640 x 240 every other row and card row shows; at 320 x 240, with the
   Classic theme (no colour map) or when the file cannot be read, the
   pattern. Before a desk accessory that finds no RAM banks the picture's
   are lent to it (wall_drop) and read again after it (at 512 KB: the
   shell's 17, the picture's 5 and an accessory's 22 do not fit with the
   fonts), without drawing anything again.

   MIT licence (see LICENSE). */
#include <veranda.h>
#include <string.h>
#include <theme.h>
#include <wallcfg.h>
#include "shell.h"
#include "winp.h"
#pragma code-name ("OVERLAY11")
#pragma rodata-name ("OVL11RO")
#pragma data-name ("OVL11DATA")
#pragma bss-name ("OVL11BSS")

#define NDIRTY 8
#define BROWS  8160u            /* a bitmap bank: 102 rows */

static unsigned char cfg[WALL_CFGLEN];          /* WALL_CFG as read */
static unsigned char key[WALL_CFGLEN];          /* the picture shown: its name and folder, */
static unsigned char keyDark;                   /* its variant */
static unsigned char isShown = 0;                /* key is the picture shown (DATA: an
                                                   overlay's BSS is not cleared) */
static unsigned char loaded = 0;                /* the banks hold it */
static unsigned char wcol[WALL_COLOURS];        /* its colours, a byte a card */
static unsigned char wallPal[8];                /* its palette entries 12-15 */
static unsigned char dx0[NDIRTY], dy0[NDIRTY], dx1[NDIRTY], dy1[NDIRTY];
static unsigned char nd = 0;

/* the kernel reads strings and buffers below $A000 only: this overlay's
   own (at $A000) go through the resident part's str, buf and msg */
static char *k(const char *s)
{
    strcpy(str, s);
    return str;
}

/*---------------------------------------------------------------- the file */

static void defaults(void)
{
    memset(cfg, 0, sizeof cfg);
    cfg[0] = 'W';
    cfg[1] = 'P';
    cfg[2] = 1;
    cfg[WALL_KIND] = WALL_PICTURE;
    cfg[WALL_PAT] = deskPat;
    strcpy((char *)cfg + WALL_NAME, WALL_DEFAULT);
    strcpy((char *)cfg + WALL_DIR, VR_WALLDIR);
}

/* WALL_CFG into cfg (the defaults without one) */
static void readCfg(void)
{
    unsigned char h;
    defaults();
    vr_cd(VR_DATA);
    h = FOpen(k(WALL_CFG), 'r');
    if (h >= 2 && h <= 9) {
        if (FRead(h, buf, WALL_CFGLEN) == WALL_CFGLEN && buf[0] == 'W' && buf[1] == 'P' && buf[2] == 1) {
            memcpy(cfg, buf, WALL_CFGLEN);
            cfg[WALL_NAME + WALL_NAMELEN - 1] = 0;
            cfg[WALL_DIR + WALL_DIRLEN - 1] = 0;
        }
        FClose(h);
    }
    ChDir(curDirName);
}

/* cfg written (the shell's options > desktop pattern) */
static void writeCfg(void)
{
    unsigned char h;
    vr_cd(VR_DATA);
    h = FOpen(k(WALL_CFG), 'w');
    if (h >= 2 && h <= 9) {
        memcpy(buf, cfg, WALL_CFGLEN);
        FWrite(h, buf, WALL_CFGLEN);
        FClose(h);
    }
    ChDir(curDirName);
}

static void say(const char *what, const char *more)
{
    if (!testing)
        return;
    strcpy(msg, "\nSHELL WALLPAPER ");
    strcat(msg, what);
    if (more) {
        strcat(msg, " ");
        strcat(msg, more);
    }
    LogText(msg);
}

/* the emulator's cycle counter (a read of $9FB8 latches it) */
static unsigned long cycles(void)
{
    unsigned long c;
    unsigned char *p = (unsigned char *)&c;
    p[0] = *(volatile unsigned char *)0x9FB8;
    p[1] = *(volatile unsigned char *)0x9FB9;
    p[2] = *(volatile unsigned char *)0x9FBA;
    p[3] = *(volatile unsigned char *)0x9FBB;
    return c;
}

/* its palette entries 12-15 (VRAM $1FA18) */
static void palette(void)
{
    unsigned char i;
    *(volatile unsigned char *)0x9F25 = 0;
    *(volatile unsigned char *)0x9F20 = 0x18;
    *(volatile unsigned char *)0x9F21 = 0xFA;
    *(volatile unsigned char *)0x9F22 = 0x11;
    for (i = 0; i < 8; ++i)
        *(volatile unsigned char *)0x9F23 = wallPal[i];
}

/* the picture of cfg, the variant of the mode, into the banks; 0 if it
   cannot be read */
static unsigned char load(void)
{
    unsigned char h, i, ok = 0, v;
    unsigned long t0 = cycles();
    if (!wallBank) {
        wallBank = BankAlloc(5);
        if (!wallBank) {
            say("NO BANKS", 0);
            return 0;
        }
    }
    loaded = 0;
    if (ChDir(k((char *)cfg + WALL_DIR)) && ChDir(k(VR_WALLDIR)) && ChDir(k(VR_SYSDIR)))
        ChDir(k("/"));
    strcpy(tmp, (char *)cfg + WALL_NAME);
    strcat(tmp, ".cvt");
    h = FOpen(tmp, 'r');
    if (h < 2 || h > 9) {
        ChDir(curDirName);
        say("MISSING", tmp);
        return 0;
    }
    if (FRead(h, buf, 508 + WALL_HEAD) == 508 + WALL_HEAD && !memcmp(buf + 508, WALL_MAGIC, 4)
        && buf[512] == 1 && buf[514] == 80 && buf[515] == 60) {
        v = theme.dark && buf[513] > 1;
        if (!FSeek(h, 508 + WALL_HEAD + (v ? WALL_VARIANT : 0), 0)
            && FRead(h, buf, 8) == 8
            && FRead(h, wcol, WALL_COLOURS) == WALL_COLOURS) {  /* (into this bank: FRead
                                                                   writes to the bank mapped) */
            memcpy(wallPal, buf, 8);
            ok = 1;
            for (i = 0; i < 5 && ok; ++i)
                ok = bfread(wallBank + i, h, i < 4 ? BROWS : WALL_BITMAP - 4 * BROWS) == (i < 4 ? BROWS : WALL_BITMAP - 4 * BROWS);
        }
    }
    FClose(h);
    ChDir(curDirName);
    if (!ok) {
        say("BAD", tmp);
        return 0;
    }
    loaded = 1;
    if (testing) {
        t0 = cycles() - t0;
        strcpy(str, v ? "DARK, READ IN " : "LIGHT, READ IN ");
        num(t0, str + strlen(str));
        say(tmp, str);
    }
    return 1;
}

/*---------------------------------------------------------------- the calls */

/* WALL_CFG read; the desktop's kind set (wallMode), the picture read
   when it changed, its palette entries set (after look_load: the theme
   set entries 2-15); 1 if the desktop is not as it was shown */
unsigned char wall_load(void)
{
    unsigned char m = wallMode, pat = deskPat, same;
    readCfg();
    if (cfg[WALL_KIND] == WALL_PATTERN && cfg[WALL_PAT] < 8)
        deskPat = cfg[WALL_PAT];
    if (cfg[WALL_KIND] == WALL_PICTURE && !lookNone && SW == 640) {
        same = isShown && !memcmp(key + WALL_NAME, cfg + WALL_NAME, WALL_CFGLEN - WALL_NAME)
            && keyDark == theme.dark;
        if (!(same && loaded)) {        /* (the same one after a desk accessory: read again) */
            isShown = 0;
            if (!load()) {
                wallMode = W_PATTERN;
                return m != wallMode;
            }
            memcpy(key, cfg, WALL_CFGLEN);
            keyDark = theme.dark;
            isShown = 1;
            if (!same)
                m = 0xFF;               /* (another picture: drawn again) */
        }
        wallMode = W_PICTURE;
        palette();
    } else
        wallMode = cfg[WALL_KIND] == WALL_PLAIN ? W_PLAIN : W_PATTERN;
    if (m != wallMode)
        say(wallMode == W_PICTURE ? "SHOWN" : wallMode == W_PLAIN ? "PLAIN" : "PATTERN", 0);
    return m != wallMode || pat != deskPat;
}

/* the next pattern (options > desktop pattern): the desktop's kind
   becomes the pattern, kept in WALL_CFG */
void wall_pattern(void)
{
    readCfg();
    if (wallMode == W_PATTERN)
        deskPat = (deskPat + 1) & 7;
    cfg[WALL_KIND] = WALL_PATTERN;
    cfg[WALL_PAT] = deskPat;
    writeCfg();
    wallMode = W_PATTERN;
    say("PATTERN", 0);
}

/* the picture's banks given back (a desk accessory needs them) */
unsigned char wall_drop(void)
{
    if (!wallBank)
        return 0;
    BankFree(wallBank);
    wallBank = 0;
    loaded = 0;
    say("DROPPED", 0);
    return 1;
}

/* the picture in pixels l-r (whole bytes), rows t-b, on the screens
   dispBufferOn selects; its cards marked for wall_flush */
void wall_fill(unsigned int l, unsigned int t, unsigned int r, unsigned int b)
{
    unsigned char bx = l >> 3, k, tall = SH > 240;
    unsigned int y = tall ? t : t << 1;          /* the picture's first row */
    wbBank = wallBank + y / 102;
    wbPtr = (unsigned char *)0xA000 + (y % 102) * 80 + bx;
    wbStep = tall ? 80 : 160;
    wbPer = tall ? 102 : 51;
    wbLeft = (102 - y % 102) / (tall ? 1 : 2);
    wbN = (r >> 3) - bx + 1;
    wbRows = b - t + 1;
    wbV = t * 80 + bx;
    wbSel = dispBufferOn;
    wall_blit();
    /* the cards (merged into the last when there are too many) */
    if (nd < NDIRTY) {
        k = nd++;
        dx0[k] = bx;
        dy0[k] = t >> 3;
        dx1[k] = r >> 3;
        dy1[k] = b >> 3;
        return;
    }
    k = NDIRTY - 1;
    if (bx < dx0[k])
        dx0[k] = bx;
    if (t >> 3 < dy0[k])
        dy0[k] = t >> 3;
    if (r >> 3 > dx1[k])
        dx1[k] = r >> 3;
    if (b >> 3 > dy1[k])
        dy1[k] = b >> 3;
}

/* a window's shadow on the picture: wall_shade (shade.s, this overlay) */

/* the colours of the cards drawn since the last time (win_colours: before
   the icons' and the windows') */
void wall_flush(void)
{
    unsigned char i, cy, n, k, m, tall = SH > 240;
    unsigned char *d;
    for (i = 0; i < nd; ++i) {
        n = dx1[i] - dx0[i] + 1;
        cy = dy0[i] < 2 ? 2 : dy0[i];
        while (cy <= dy1[i]) {          /* (as many rows a ColorRect as buf holds: V81) */
            m = sizeof buf / n > 60 ? 60 : sizeof buf / n;
            if (m > dy1[i] - cy + 1)
                m = dy1[i] - cy + 1;
            for (k = 0, d = buf; k < m; ++k, d += n)
                memcpy(d, wcol + (tall ? cy + k : (cy + k) << 1) * 80 + dx0[i], n);
            r0 = (unsigned int)buf;
            ColorRect(dx0[i], n, cy, m, COLOR_BUF);
            cy += m;
        }
    }
    nd = 0;
}
