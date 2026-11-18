/* Veranda shell: what its parts share.

   The shell is a resident part (main.c, look.c, ovl.s, play.s: $0801 up)
   and banked overlays (DECISIONS V37), each run in place at $A000 of a RAM
   bank of its own:
     1 win.c    the windows, the desktop, the menu bar's clock (drawing)
     2 view.c   a window's contents: large icons, small icons, the list
     3 dir.c    reading a folder, the status line's numbers, the desk file
     4 files.c  find, copy, move, the trash, get info (DECISIONS V61)
     5 print.c  the printer: a picture dropped on it printed to a PDF
     6 mouse.c  presses, drags and drops
     7 wops.c   a window opened, closed, moved, resized, zoomed; the clock
     8 chrome.c a window's chrome (Look 2, DECISIONS V77): the frame,
                the title band, the toolbar; what a press there is
     9 bars.c   the sidebar, the status bar, the shadow, the chrome's
                colours, the zoom box
    10 tiles.c  the tiles: the shell's own (tiles.h) and the
                applications' (their files'), drawn; the small glyphs
    11 wall.c   the desktop's picture (Look 2, DECISIONS V78)
       dock.c   (with tiles.c) the dock: the switcher's programs (V83)
   Code in one overlay calls another's through the resident trampoline
   (XCALL: ovl_go maps the bank and makes it appBank, so the kernel's
   callbacks find it, and maps the caller's back). Strings, tables and
   buffers live in the resident part's memory (the kernel takes pointers
   below $A000 only). A window's entries live in a RAM bank of its own
   (64 bytes each), copied out one at a time (ent_get into E). */
#ifndef SHELL_H
#define SHELL_H
#include <vrpath.h>             /* the card's folders (DECISIONS V71) */

#define NAMELEN  50             /* a name kept (the entry's 64 bytes; 40 before V82) */
#define PATHMAX  61             /* a path's characters (curDirName: 0-61) */
#define MAXE     120            /* a window's entries */
#define MAXICON  48             /* a window's GEOS files with their own icon */
#define NWIN     2
#define MAXPINS  4
#define PINDIR   62
#define PINNAME  (LONG_MAX + 5)  /* a pin's file name: a long GEOS name, ".cvt", 0 (22 in the desk file's version 1; V82) */
#define MODEBYTE 63             /* curDirName: the video mode + 1 */
#define VIEWBYTE 62             /* deskPath: window 0's view, sort, pattern */
#define FINDLEN  15             /* a search's characters */

#define RAMBANK (*(volatile unsigned char *)0)

extern unsigned char play_busy;

/* an entry (64 bytes in the window's bank) */
struct ent {
    char type;                  /* 'D', 'F', 'U' (..) */
    unsigned char icon;         /* I_DOC, I_FOLDER, I_UP; I_OWN + n */
    unsigned char geos;         /* GEOS type, 0 if not a GEOS file */
    unsigned char tile;         /* its tile: TL_* (Look 2) */
    unsigned long size;         /* bytes */
    unsigned char date[5];      /* year (since 1900), month, day, hour, minute; month 0: none */
    char name[NAMELEN + 1];
};
#define I_DOC    0
#define I_FOLDER 1
#define I_UP     2
#define I_OWN    3

/* an entry's tile (DECISIONS V77) */
#define TL_BUILT 0x80           /* + n: the shell's tile n (tiles.h: a port's, a folder ...) */
#define TL_OWN   0x40           /* + n: the application's own, slot n of the tiles' bank */
#define TL_ICON  0x20           /* its GEOS icon (I_OWN + n) on a chrome tile */
#define MAXTILE  31             /* own tiles a window (129 bytes each, window w's at w * 4096) */
#define TILELEN  129            /* the colour, 32 x 32 pixels */
#define TT_AT    1000           /* the desk bank: the tiles (TL_*) of the programs put aside, TASK_MAX bytes (V84);
                                   their icons are the desk bank's MAXPINS + n, their own tiles slot MAXPINS + n */

enum { V_ICON, V_SMALL, V_LIST, VIEWS };
enum { S_NAME, S_SIZE, S_DATE, S_TYPE };

/* a window; x and w in cards, y and h in pixels (multiples of 8) */
struct win {
    unsigned char open;
    unsigned char x, w;
    unsigned int y, h;
    unsigned char view, sort;
    unsigned char n, sel, first;        /* entries, the selection (an order index; $FF none), the first shown */
    unsigned char nicons, nfiles;
    unsigned char bank;                 /* its entries; at HISTAT its history */
    unsigned long used;                 /* the files' bytes */
    unsigned char hn, hc;               /* the history: paths kept, the one shown */
    unsigned char zx, zw;               /* zoomed: the place before (zw 0: not zoomed) */
    unsigned int zy, zh;
    char find[FINDLEN + 1];             /* the search: names holding it */
    char path[PATHMAX + 1];
    unsigned char order[MAXE];
};
#define HISTAT  (MAXE * 64)             /* the history in the window's bank: */
#define HISTN   8                       /* 8 paths of 64 bytes */
extern struct win wins[NWIN];
extern unsigned char front;             /* the window in front ($FF: none) */
extern unsigned char bIcons;            /* the GEOS icons: window w's at w * 4096 */
extern unsigned char bDesk;             /* the pinned applications' icons; at 1024 their tiles */
extern unsigned char bTiles;            /* the applications' own tiles: window w's at w * 4096 */
extern unsigned char bWin, bView, bDir, bFiles, bPrint, bMouse, bOps, bChrome, bBars, bTile, bWall, bDock;   /* the overlays' banks */
extern unsigned char tileBuf[132];      /* a tile or a glyph as BitmapUp takes it */
extern unsigned char pinTile[MAXPINS];  /* the pins' tiles (TL_*) */

extern struct ent E, E2;                /* entries copied out */
extern unsigned char pic[64];           /* an icon copied out */
extern char cardName[18];               /* the card's name (the listing's header) */
extern unsigned long cardFree;          /* bytes free on the card */
extern unsigned char deskPat;           /* the desktop's pattern (0-7) */
extern char tmp[NAMELEN + 24];
extern char str[NAMELEN + 24];
extern char msg[160];
extern unsigned char buf[1024];

/* the pins: applications dragged onto the desktop */
struct pin {
    char dir[PINDIR];
    char name[PINNAME];
};
extern struct pin pins[MAXPINS];
extern unsigned char npins;
extern unsigned char deskDirty;         /* the desk file is to be written */
extern unsigned char dockMin;           /* the windows minimised to the dock (bit w; V83) */

/* ovl.s: the trampoline */
extern unsigned char ovl_bk;
extern void *ovl_fn;
void ovl_go(void);
#define XCALL(b, f, T) ((T)(ovl_bk = (b), ovl_fn = (void *)(f), (void *)ovl_go))
typedef void (*FV)(void);
typedef void (*FP)(const char *);
typedef void (*FB)(unsigned char);
typedef void (*FBB)(unsigned char, unsigned char);
typedef unsigned char (*RB)(unsigned char);

/* main.c */
void bput(unsigned char b, unsigned int off, const void *src, unsigned int n);
void bget(unsigned char b, unsigned int off, void *dst, unsigned int n);
unsigned int bfread(unsigned char b, unsigned char h, unsigned int n);
unsigned char bfwrite(unsigned char b, unsigned char h, unsigned int n);
void ent_get(unsigned char w, unsigned char i);     /* E = entry i of window w */
void ent_get2(unsigned char w, unsigned char i);    /* E2 = ... */
void ent_put(unsigned char w, unsigned char i);     /* entry i = E */
const unsigned char *icon_get(unsigned char w, unsigned char n, unsigned char docs);
void icon_put(unsigned char w, unsigned char n, const unsigned char *icon, const unsigned char *docs);
void sort_win(unsigned char w);
void type_str(const struct ent *e, char *out);      /* the list's type column */
void shown(const struct ent *e, char *out);         /* a GEOS file's name without ".cvt" */
unsigned char shell_is_cvt(const char *n);
void num(unsigned long n, char *out);
void shell_say(const char *m);                    /* a message box */
void shell_warn(const char *m);                   /* the beep and a message */
void shell_err(const char *what, unsigned char e);
unsigned char shell_yes(const char *line1, const char *line2);    /* YES / NO */
void shell_reload(void);                          /* the windows read and drawn again */
void shell_redraw(void);                          /* the front window's contents drawn again */
void shell_go(const char *dir, const char *name, unsigned char open);
void shell_open(unsigned char w, unsigned char idx);  /* a double click on an entry */
void shell_front(unsigned char w);                /* curDirName = window w's path */
void shell_pin_start(unsigned char p);            /* a pinned application started */
void shell_trash(void);                           /* the trash's window */
void shell_desk_save(void);                       /* the desk file, if changed */
void startDoc(const char *dir);                     /* main.c: the application tmp in dir, the document str's */
extern const char *const homes[3];                  /* where applications are looked for (V71) */
void dir_open_data(void);                           /* dir.c: a GEOS document (E) with its parent application (V87) */
void shell_keep(void);                            /* its folder and the desk file kept: a program starts */
extern unsigned char testing;                     /* a test script plays */

/* win.c (overlay 1) */
void win_init(void);                              /* the layout for the video mode */
void win_all(void);                               /* the whole screen */
void win_redraw(unsigned char w);                 /* window w drawn again where it shows */
void win_view(unsigned char w);                   /* its contents in another view (V81) */
void win_content(unsigned char w);                /* its contents, status and footer */
void win_status(unsigned char w);
void win_open(unsigned char w);                   /* opened (its path read) and drawn */
void win_close(unsigned char w);
void win_press(void);
void win_idle(void);
void win_unpin(void);
void win_behind(unsigned char w);                 /* its title band: gone behind */
const char *chrome_place(unsigned char i);        /* chrome.c: a place's folder */
void chrome_goplace(unsigned char w, unsigned char i);  /* chrome.c: a place of the sidebar (V84) */
extern char modsDir[PATHMAX + 1];                 /* chrome.c: the Mods place's folder (V84) */
extern unsigned int cx0, cy0, cx1, cy1;           /* the front window's content area */

/* view.c (overlay 2) */
void view_draw(unsigned char w);                  /* the content area */
void view_colours(unsigned char w);               /* its colours (Look 2) */
unsigned char view_hit(unsigned char w, unsigned int x, unsigned int y);   /* an order index, $FF none, $FE header */
void view_select(unsigned char w, unsigned char s);
void view_cell(unsigned char w, unsigned char s); /* vx, vy: the cell's corner (on its page) */
void view_page(unsigned char w, signed char d);   /* +1, -1: pages; +2, -2 rows (the list) */
unsigned char view_per(unsigned char w);
extern unsigned int vx, vy;

/* dir.c (overlay 3) */
void view_log(unsigned char w);                   /* the list's rows on the console */
void view_header(unsigned char w, unsigned int x); /* a click on the list's header */
void dir_read(unsigned char w);                   /* window w's path read */
void dir_status(unsigned char w, char *out);      /* the status line */
void desk_load(void);
void desk_save(void);
void pins_icons(void);
void pin_add(void);                               /* E (an application of the front window) pinned */
void pin_remove(unsigned char i);
void mods_set(void);                              /* the front window's folder: the Mods place (V84) */
void mods_load(void);                             /* MODS.CFG read (V84) */

/* files.c (overlay 4): find, copy and move (whole folders), the trash, get info */
void files_init(void);
extern unsigned char found_on;                    /* the find results are shown */
void found_draw(void);
void found_press(unsigned int x, unsigned int y);
void files_title(unsigned int x, unsigned int y); /* what is picked up */
void files_find(void);
void files_pick(const char *name, char type, unsigned char move);
void files_put(void);
void files_drop(const char *from, const char *name, char type, const char *to, unsigned char move);
void files_delete(const char *name, char type, unsigned char ask);
void files_empty_trash(void);
void files_show_trash(void);
void files_info(const char *name, char type);
extern char trash[24];
void trash_path(void);
unsigned char trash_full(void);

/* print.c (overlay 5) */
void print_drop(const char *name);

/* look.c: the colours, from the theme (DECISIONS V65) */
unsigned char look_load(void);                    /* 1: another theme than shown */
void look_off(void);                              /* Classic: the colour map hidden */
void look_rect(unsigned char x, unsigned char w, unsigned char y, unsigned char h, unsigned char c);
void shell_rframe(unsigned int l, unsigned int t, unsigned int r, unsigned int b, unsigned char rad);
extern unsigned char lookNone;                    /* Classic: no colours */
unsigned char shell_seen(unsigned int l, unsigned int t, unsigned int r, unsigned int b);
extern unsigned char colClip, colX0, colX1, colY0, colY1;  /* look_rect: only these cards (V81) */

/* main.c: Look 2's navigation (DECISIONS V77) */
void shell_hist(unsigned char w);                 /* window w's path kept in its history */
void shell_nav(unsigned char w, signed char d);   /* back (-1), forward (+1) */
unsigned char shell_goto(unsigned char w, const char *p);   /* window w shows folder p; 0: it did not open */
void shell_crumb(unsigned char w, unsigned char len); /* the path's first len characters */
void shell_search(unsigned char w);               /* the search field */
void shell_setview(unsigned char v);              /* the front window's view */

/* tiles.c (overlay 9): the tiles (DECISIONS V77) */
unsigned char tile_draw(unsigned char cx, unsigned int y, unsigned char w, unsigned char t, unsigned char icon);
unsigned char tile_colour(unsigned char w, unsigned char t);
unsigned char tile_class(const unsigned char *cls);   /* TL_BUILT + n, or 0 */
void mini_draw(unsigned int x, unsigned int y, unsigned char m);

/* wall.c (overlay 11): the desktop's picture (DECISIONS V78) */
#define W_PICTURE 0                               /* wallMode: the picture */
#define W_PLAIN   1                               /* the theme's desktop colour */
#define W_PATTERN 2                               /* a pattern on it (also: no picture) */
extern unsigned char wallMode;                    /* what the desktop shows */
extern unsigned char wallBank;                    /* the picture's bitmap (5 banks; 0: none) */
unsigned char wall_load(void);                    /* after look_load: 1 if the desktop changed */
void wall_pattern(void);                          /* options > desktop pattern */
unsigned char wall_drop(void);                    /* its banks given back (1 if it had them) */
void wall_fill(unsigned int l, unsigned int t, unsigned int r, unsigned int b);
void wall_flush(void);                            /* the colours of what wall_fill drew */
/* wallblit.s (resident): the picture's rows to the screens */
extern unsigned char wbBank, wbN, wbSel, wbStep, wbLeft, wbPer;
extern unsigned int wbRows, wbV;
extern unsigned char *wbPtr;
#pragma zpsym ("wbPtr")
void wall_blit(void);
void wall_shade(unsigned int l, unsigned int t, unsigned int r, unsigned int b);  /* a shadow on it */

/* dock.c (overlay 10, with tiles.c): the dock (DECISIONS V83) */
void dock_layout(void);
void dock_draw(unsigned int l, unsigned int t, unsigned int r, unsigned int b);
void dock_colours(void);
void dock_init(void);
void dock_note(const char *dir);                  /* the program tmp in dir: its path, the switcher's note (V84) */
void dock_min(unsigned char w);
unsigned char dock_press(unsigned int x, unsigned int y, unsigned char w);

#endif
