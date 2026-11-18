/* VeraBASIC Run (DECISIONS V86): the runtime alone. It opens a program
   document (vbasic.py build: GEOS class "VeraBASIC   V1.0", parent class
   "VBRun       V1.0"), lays its records out in RAM banks and runs it in
   slices from appMain (vb.s); PRINT and INPUT go to the console, a Look 2
   window of 8 x 8 cards drawn here with PutCards and scrolled with
   ScrollRect.

   The test build (VB_TEST, tools/gate_vb_core.sh) runs the programs that
   VBTEST.LST names one after the other, each with its keys (NAME.KEY),
   and logs their console lines ("VB| ...") for the reference interpreter
   to be compared with; a line "!stop n NAME" presses Ctrl+C (the ROM's
   key queue: STOP) after n slices, "!aside n NAME" puts the program aside
   (TaskCtl) after n slices, and "!shot" ends with a VRAM dump; "!fill n"
   fills every free RAM bank with bytes at random from the seed n (vb.s:
   what a program then finds in the banks it gets).

   MIT licence (LICENSE). */
#include <veranda.h>
#include <vr.h>
#include <theme.h>
#include <vrwin.h>
#include <string.h>

#define RAMBANK (*(volatile unsigned char *)0)
#define CON_W 78
#define CON_H 54
#define CON_X 1
#define CON_Y 4

/* vb.s */
extern unsigned char vb_tiny, vb_state, vb_how, vb_errno, vb_test, vb_stopreq;
extern unsigned int vb_erl, vb_slices;
extern unsigned char pbase, npb, sbank, mbank, nprocs, limitb, nbanks, vb_relbank;
extern unsigned int cellbase, proctab, ndata, dynbase, vb_reln;
extern unsigned char cbank, tbank, ctop, ccy, ccol, cscroll, cdirty[], cwait;
extern unsigned char *scr_ptr, *scr_end;
extern unsigned char kq[], kqh, kqt;
extern unsigned int gc_count;
extern unsigned long vb_idlecyc;
void vb_slice(void);
void vb_wd(void);
void vb_key(void);
void vb_begin(void);
void vb_reset(void);
void vb_free(void);
void con_reset(void);
void vb_rewrite(void);

extern unsigned char _BSS_RUN__[], _BSS_SIZE__[];
extern unsigned int vb_dynsize;
/* vb.s: reading into a bank from code that runs in the overlay's */
extern unsigned char vb_fbank;
extern unsigned int vb_faddr, vb_fcount;
unsigned char __fastcall__ vb_readrec(unsigned char bank);
unsigned int __fastcall__ vb_fread(unsigned char handle);
/* vbtok.s, vbprep.s: the tokenizer (overlays 2-4) */
extern unsigned char vt_ovl2, vt_ovl3, vt_ovl4, vt_ovl5, vt_err, vt_recb[4];
extern unsigned int vt_srclen, vt_size[4], vt_lines;
extern const char *vt_name;
extern char vt_msg[];
#ifdef VB_TEST
void __fastcall__ vb_tfill(unsigned int seed);
#endif
void vb_compile(void);
void vb_dump(void);
/* the Veranda extensions (phase 3, DECISIONS V89): vbx.s, vbev.s, vbui.s */
extern unsigned char vx_ovl6, vx_ovl7, vx_any, vx_hide, vx_used, vx_idle, vx_evn;
/* phase 4 (DECISIONS V90): overlay 8 (dialogs, icons, pictures, sprites),
   9 (files), the C overlays A and B (records 10, 11: vbc.cfg) */
extern unsigned char vx_ovl8, vx_ovl9, vx_ovla, vx_ovlb;
void vx_end(void);
extern unsigned int vx_cellend;
void vx_reset(void);
void __fastcall__ vrw_win(const struct vrwin *w, const char *title);
void __fastcall__ vrw_title(const struct vrwin *w, const char *title);
void __fastcall__ vrw_status(const struct vrwin *w, const char *left, const char *right);
#define vr_win vrw_win
#define vr_win_title vrw_title
#define vr_win_status vrw_status
void vx_irq(void);
void ev_keyv(void);
void ev_press(void);
unsigned char vx_quit(void);
void vb_slice(void);

#ifdef VB_IDE
/* VeraBASIC, the editor (DECISIONS V93): this program with the editor
   (apps/vbasic/ide) round it. Its main RAM part takes the interpreter's
   code's place (vb.s _ide_s0 .. _ide_s1) while the editor shows; ide_go
   and ide_back here change them over (ide/ideshare.h) */
#include "ide/ideshare.h"
extern unsigned char ide_s0[], ide_s1[];
extern unsigned char vt_inmem, vt_src;
extern unsigned int vt_after, vb_esrc, vt_erl;
unsigned char ide_req, ide_res, ide_opts, ide_ovl1, edit_idle, test_log;
unsigned char ide_banks[IDE_NBANKS];
unsigned int ide_govec;
#endif

/* the code that runs once, or seldom (loading, the test's list, the
   window's chrome: vrwin.c compiled into it): overlay 1, appBank, mapped
   whenever the kernel calls the program; the interpreter and the console's
   drawing stay in main RAM, which the program's banks share $A000 with */

static const struct vrwin win = { 0, 2, 80, 58, 0, 2, 0 };
unsigned char vb_font[2048];      /* (CARDS draws with it too) */
static char title[80];
char docname[70];                  /* (the menu bar's name: vbui.s) */
static unsigned char raw, nraw;         /* a run of banks to read records into */
static unsigned char ended;

/* ---- the console's drawing ---- */
static unsigned char rowbuf[CON_W];

/* screen row y's text (in cbank) into rowbuf */
static unsigned char *row_text(unsigned char y)
{
    unsigned char r = ctop + y;
    if (r >= CON_H)
        r -= CON_H;
    BankCopy((void *)(0xA000 + r * CON_W), rowbuf, CON_W, cbank, 0);
    return rowbuf;
}

void con_flush(void)
{
    unsigned char y, keep = dispBufferOn;
    if (vx_hide)                        /* the program's own screen */
        return;
    dispBufferOn = ST_WR_FORE;
    if (cscroll >= CON_H)
        memset(cdirty, 1, CON_H);
    else if (cscroll)
        ScrollRect(CON_X, CON_W, CON_Y * 8, (CON_Y + CON_H) * 8 - 1, -8 * (int)cscroll, 0);
    cscroll = 0;
    for (y = 0; y < CON_H; ++y)
        if (cdirty[y]) {
            cdirty[y] = 0;
            PutCards(row_text(y), vb_font, CON_X, CON_W, (CON_Y + y) * 8, 0);
        }
    if (cwait && ccol < CON_W) {
        PutCards(row_text(ccy) + ccol, vb_font, CON_X + ccol, 1, (CON_Y + ccy) * 8, 1);
        cdirty[ccy] = 1;
    }
    dispBufferOn = keep;
}

/* Open program...: the kernel's file box (DBGETFILES: the GEOS documents
   of class VeraBASIC in the current folder, by their 16-character GEOS
   names: the long-name dialog, with what it needs of the C library, would
   take 1.6 KB of the 2.4 KB of main RAM a program's names have; long
   names come with the shell's double click, DECISIONS V87) */
#ifndef VB_IDE
struct filedlg {
    unsigned char pos;
    unsigned char f, fx, fy;
    unsigned char op, opx, opy;
    unsigned char cn, cnx, cny;
    unsigned char end;
};
static const struct filedlg openbox = {
    DEF_DB_POS | 1,
    DBGETFILES, 4, 4,
    OPEN, DBI_X_2, DBI_Y_0,
    CANCEL, DBI_X_2, DBI_Y_2,
    0
};
static char openname[17];
static const char vbclass[] = "VeraBASIC";
#endif

#pragma code-name (push, "OVERLAY1")
/* the X16 ROM's 8 x 8 ISO font (ROM bank 6), as Veranda Edit takes it */
static const unsigned char sig0[8] = { 0xC3, 0x99, 0x91, 0x91, 0x91, 0x9F, 0xC1, 0xFF };
static const unsigned char sigA[8] = { 0x18, 0x3C, 0x24, 0x66, 0x7E, 0x66, 0x66, 0x00 };

void vb_fontload(void)              /* (overlay 8 too: a C overlay ran in the font's place) */
{
    unsigned char keep, ok = 0;
    unsigned int a;
    asm("sei");
    keep = *(unsigned char *)1;
    *(unsigned char *)1 = 6;
    for (a = 0xC000; a; a += 0x800)
        if (!memcmp((void *)a, sig0, 8) && !memcmp((void *)(a + 0x41 * 8), sigA, 8)) {
            memcpy(vb_font, (void *)a, 2048);
            ok = 1;
            break;
        }
    if (!ok)
        memcpy(vb_font, (void *)0xC800, 2048);
    *(unsigned char *)1 = keep;
    asm("cli");
}

static void status(const char *left, const char *right)
{
    if (vx_hide)                        /* the program's own screen */
        return;
    vr_win_status(&win, left, right);
}

/* ---- menus and a message box ---- */
struct item  { const char *text; unsigned char type; void *p; };
struct menu1 { unsigned char top, bottom; unsigned int left, right; unsigned char flags; struct item i[1]; };
struct menu2 { unsigned char top, bottom; unsigned int left, right; unsigned char flags; struct item i[2]; };

struct menu3 { unsigned char top, bottom; unsigned int left, right; unsigned char flags; struct item i[3]; };
static void m_about(void);
static void m_quit(void);
static void m_stop(void);
#ifdef VB_IDE
/* (the editor: while its program runs, the bar is "VeraBASIC", "Run") */
static struct menu1 mrun = { 16, 16 + 1 * 16, 0, 160, VERTICAL | 1, {
    { "About VeraBASIC", MENU_ACTION, m_about } } };
static struct menu1 mprog = { 16, 16 + 1 * 16, 0, 160, VERTICAL | 1, {
    { "Stop          Ctrl+C", MENU_ACTION, m_stop } } };
static struct menu2 mbar = { 0, 15, 0, 200, HORIZONTAL | UN_CONSTRAINED | 2, {
    { "VeraBASIC", SUB_MENU, &mrun }, { "Run", SUB_MENU, &mprog } } };
#else
static void m_open(void);
static struct menu3 mrun = { 16, 16 + 3 * 16, 0, 160, VERTICAL | 3, {
    { "About VeraBASIC Run", MENU_ACTION, m_about }, { "Open program...", MENU_ACTION, m_open },
    { "Quit", MENU_ACTION, m_quit } } };
static struct menu1 mprog = { 16, 16 + 1 * 16, 0, 120, VERTICAL | 1, {
    { "Stop", MENU_ACTION, m_stop } } };
static struct menu2 mbar = { 0, 15, 0, 200, HORIZONTAL | UN_CONSTRAINED | 2, {
    { "VeraBASIC Run", SUB_MENU, &mrun }, { "Program", SUB_MENU, &mprog } } };
#endif

struct msgbox {
    unsigned char pos;
    unsigned char t1, x1, y1; const char *s1;
    unsigned char t2, x2, y2; const char *s2;
    unsigned char ok, okx, oky;
    unsigned char end;
};
static struct msgbox box = { DEF_DB_POS | 1,
    DBTXTSTR, TXT_LN_X, TXT_LN_1_Y, 0, DBTXTSTR, TXT_LN_X, TXT_LN_2_Y, 0,
    OK, DBI_X_2, DBI_Y_2, 0 };

static void message(const char *a, const char *b)
{
    box.s1 = a;
    box.s2 = b;
    DoDlgBox((void *)&box);
}

static void m_about(void)
{
    GotoFirstMenu();
    message("VeraBASIC Run 1.0", "Runs VeraBASIC programs (MIT licence).");
}

static void start(const char *name);
static void free_program(void);

#ifndef VB_IDE
static void m_quit(void)
{
    GotoFirstMenu();
    if (vb_state == 1 && vx_quit())     /* ON QUIT: the program decides */
        return;
    EnterDeskTop();
}

/* File > Open: another program (the one running stopped first) */
static void m_open(void)
{
    unsigned char r;
    GotoFirstMenu();
    openname[0] = 0;
    r5 = (unsigned int)openname;
    r7L = APPL_DATA;
    r10 = (unsigned int)vbclass;
    longNames = 0;
    r = DoDlgBox((void *)&openbox);
    if (r == DBGETFILES) {
        message("There are no VeraBASIC programs", "in this folder.");
        return;
    }
    if (r != OPEN || !openname[0])
        return;
    if (vb_state == 1) {
        vb_state = 2;
        free_program();
    }
    start(openname);
}
#endif

static void m_stop(void)
{
    GotoFirstMenu();
    if (vb_state == 1)
        vb_stopreq = 1;
}

/* ---- loading a program document ---- */
static unsigned char rwin[256];
static unsigned int winpage;

/* a byte of the raw run (through a page copied to main RAM) */
static unsigned char rb(unsigned int off)
{
    if ((off >> 8) != winpage) {
        winpage = off >> 8;
        BankCopy((void *)(0xA000 + ((winpage << 8) & 0x1FFF)), rwin, 256, raw + (winpage >> 5), 0);
    }
    return rwin[off & 255];
}

static unsigned int rw(unsigned int off)
{
    return rb(off) | (rb(off + 1) << 8);
}

/* a program made from its source (.BAS, the tokenizer): its records'
   runs of banks, and sizes */
static unsigned char srcmode;
#define recb vt_recb

static void runs_free(void)
{
    unsigned char i;
    for (i = 0; i < 4; ++i)
        if (recb[i]) {
            BankFree(recb[i]);
            recb[i] = 0;
        }
}

/* record n into the raw run: its size (0xFFFF: none, or an error) */
static unsigned int rec_read(unsigned char n)
{
    unsigned int end;
    winpage = 0xFFFF;
    if (srcmode) {
        raw = recb[n];
        return raw ? vt_size[n] : 0xFFFF;
    }
    if (PointRecord(n))
        return 0xFFFF;
    vb_fcount = nraw * 8192u - 1;
    if (vb_readrec(raw))
        return 0xFFFF;
    end = r7;
    return end - 0xA000;
}

static const char *load_error;

static void free_program(void)
{
    vx_end();                           /* its files closed, its sprites off */
    vb_free();
    if (pbase) BankFree(pbase);
    if (sbank) BankFree(sbank);
    if (mbank) BankFree(mbank);
    pbase = sbank = mbank = 0;
}

static unsigned char load(const char *name)
{
    unsigned int nsym, nproc, size, i, off, p, pt, cell, s1, s2, s3;
    unsigned char k, n, kind, type, fl, b, nslots;

    load_error = "This is not a VeraBASIC program.";
    longNames = LONG_NAMES;
    if (srcmode)
        goto made;
    if (OpenRecordFile((void *)name)) {
        load_error = "The program could not be opened.";
        return 0;
    }
    for (nraw = 8; nraw; --nraw)
        if ((raw = BankAlloc(nraw)) != 0)
            break;
    if (!nraw) {
        load_error = "Not enough memory (RAM banks).";
        CloseRecordFile();
        return 0;
    }
made:
    vb_reset();
    /* record 0: the header, the symbols, the procedures */
    s1 = rec_read(0);
    if (s1 == 0xFFFF || s1 < 24 || rb(0) != 'V' || rb(1) != 'B')
        goto bad;
    if (rb(2) > 1 || rb(3) > 1) {
        load_error = "Made by a newer VeraBASIC.";
        goto bad;
    }
    nsym = rw(4);
    nproc = rw(6);
    ndata = rw(14);
    nprocs = nproc;
    /* main RAM: the procedures (8 bytes each), their slots' types, the cells */
    proctab = dynbase;
    off = 24;
    for (i = 0; i < nsym; ++i)          /* past the symbols */
        off += 8 + rb(off + 7);
    s1 = off;                           /* the procedures */
    pt = proctab + nproc * 8;
    for (i = 0; i < nproc; ++i) {
        p = proctab + i * 8;
        nslots = rb(off + 7);
        *(unsigned char *)p = rb(off + 6);
        *(unsigned char *)(p + 1) = nslots;
        *(unsigned char *)(p + 2) = rb(off + 8) & 1;
        *(unsigned int *)(p + 6) = pt;
        off += 9 + 3 * nslots;
        pt += nslots;
    }
    cellbase = (pt + 7) & ~7u;
    vx_cellend = cellbase + nsym * 8;
    if ((unsigned long)cellbase + (unsigned long)nsym * 8 > (unsigned int)_BSS_RUN__ + (unsigned int)_BSS_SIZE__ + vb_dynsize) {
        load_error = "Too many names for main RAM.";
        goto bad;
    }
    /* the cells: byte 7 the type, 6 a mark for vb_rewrite (1 label, 2 procedure) */
    memset((void *)cellbase, 0, nsym * 8);
    off = 24;
    for (i = 0; i < nsym; ++i) {
        cell = cellbase + i * 8;
        kind = rb(off);
        type = rb(off + 1);
        if (kind & 64) {                /* a built-in constant */
            *(unsigned int *)cell = rw(off + 5);
            *(unsigned char *)(cell + 7) = 1 | 0x10;
        } else if (kind & (8 | 16)) {   /* SUB, FUNCTION: byte 0 its procedure */
            *(unsigned char *)(cell + 6) = 2;
            if (kind & 16)
                *(unsigned char *)(cell + 7) = type | 0x40;
        } else if (kind & 32) {         /* a label: its line record's offset */
            *(unsigned char *)(cell + 6) = 1;
            *(unsigned int *)cell = rw(off + 2);
        } else if (kind & 2)
            *(unsigned char *)(cell + 7) = type | 0x20;
        else
            *(unsigned char *)(cell + 7) = type;
        off += 8 + rb(off + 7);
    }
    /* the procedures' slots' types; the procedures' cells their numbers */
    off = s1;
    for (i = 0; i < nproc; ++i) {
        p = proctab + i * 8;
        nslots = rb(off + 7);
        pt = *(unsigned int *)(p + 6);
        *(unsigned char *)(cellbase + rw(off) * 8) = i;
        for (n = 0; n < nslots; ++n) {
            fl = rb(off + 11 + 3 * n);
            b = *(unsigned char *)(cellbase + rw(off + 9 + 3 * n) * 8 + 7) & 3;
            if (fl & 4) b |= 0x04;      /* a parameter */
            if (fl & 8) b |= 0x08;      /* the result */
            if (fl & 2) b |= 0x20;      /* an array */
            if (fl & 1) b |= 0x80;      /* BYREF */
            *(unsigned char *)(pt + n) = b;
        }
        off += 9 + 3 * nslots;
    }
    /* the BASIC stack and the DATA index (record 3): a bank each */
    mbank = BankAlloc(1);
    sbank = BankAlloc(1);
    if (!mbank || !sbank) {
        load_error = "Not enough memory (RAM banks).";
        goto bad;
    }
    s3 = rec_read(3);
    if (s3 == 0xFFFF)
        s3 = 0;
    if (s3 > 8192)
        goto bad;
    if (s3)
        BankCopy((void *)0xA000, (void *)0xA000, s3, raw, mbank);
    /* record 1: the lines, 7936 bytes a bank, with the next bank's first
       256 bytes after them (no line record crosses a bank) */
    size = rec_read(1);
    if (size == 0xFFFF || size == 0)
        goto bad;
    npb = (size + 7935) / 7936;
    pbase = BankAlloc(npb);
    if (!pbase) {
        load_error = "Not enough memory (RAM banks).";
        goto bad;
    }
    for (k = 0; k < npb; ++k) {
        unsigned long from = (unsigned long)k * 7936;
        unsigned int cnt = size - (unsigned int)from;
        if (cnt > 8192)
            cnt = 8192;
        BankCopy((void *)(0xA000 + ((unsigned int)from & 0x1FFF)), (void *)0xA000, cnt,
                 raw + (unsigned char)(from >> 13), pbase + k);
    }
    /* record 2: the line index, for the line references */
    s2 = rec_read(2);
    if (s2 == 0xFFFF)
        s2 = 0;
    vb_relbank = raw;
    vb_reln = s2 / 4;
    vb_rewrite();
    if (srcmode)
        runs_free();
    else {
        BankFree(raw);
        CloseRecordFile();
    }
    raw = 0;
    vb_begin();
    return 1;
bad:
    if (srcmode)
        runs_free();
    else {
        if (raw)
            BankFree(raw);
        CloseRecordFile();
    }
    raw = 0;
    free_program();
    return 0;
}

/* name a .BAS source? */
static unsigned char is_bas(const char *name)
{
    const char *e = name;
    while (*e)
        ++e;
    e -= 4;
    return e > name && e[0] == '.' && (e[1] | 0x20) == 'b' && (e[2] | 0x20) == 'a' && (e[3] | 0x20) == 's';
}

/* a .BAS file: vbprep.s's compile reads its text into banks, the
   tokenizer and prepare make the four records in runs of banks (recb);
   0 and load_error on an error */
static unsigned char compile(const char *name)
{
    srcmode = 0;
    vt_name = name;
    vb_compile();
    switch (vt_err) {
    case 0:
        srcmode = 1;
        return 1;
    case 1:
        load_error = vt_msg;            /* "Line n: message" */
        break;
    case 2:
        load_error = "The program could not be opened.";
        break;
    case 3:
        load_error = "Not enough memory (RAM banks).";
        break;
    default:
        load_error = "The program is too big (64 KB of text at most).";
    }
    return 0;
}

static void start(const char *name)
{
    if (vx_used | vx_hide) {            /* the last program's look gone */
        dispBufferOn = ST_WR_FORE | ST_WR_BACK;
        clipOn = 0;
        theme_palette();
        DoMenu((void *)&mbar, 0);
        vr_win(&win, "VeraBASIC Run");
    }
    vx_reset();
    strcpy(docname, name);
    strcpy(title, "VeraBASIC Run: ");
    strcat(title, name);
    vr_win_title(&win, title);
    con_reset();
    con_flush();
    ended = 0;
    srcmode = 0;
    if ((!is_bas(name) || compile(name)) && load(name)) {
        status(name, "Running (Ctrl+C stops it)");
    } else {
        status(load_error, "");
        vb_state = 2;
        vb_how = 3;
    }
}

#ifdef VB_IDE
/* ---- VeraBASIC, the editor: the change of places (ide/ideshare.h) ----
   ide_go (appMain for one pass, overlay 1 mapped: LOW jumps here) puts
   the interpreter's code back at _ide_s0 and the editor's part into its
   banks, checks the text the editor left in vt_src and, for Run, saves the
   editor's screen and starts the program in Run's console window;
   ide_back gives the editor its place, its screen and its vectors again
   and calls it (LOW's first bytes: jmp ide_enter) */
static unsigned char ide_es[3];             /* the other part: the editor's, or the code */
static unsigned char ide_sb;                /* the editor's screen: a run of 6 banks (0 none) */
static unsigned int ide_ss;                 /* _ide_s1 - _ide_s0 */
static unsigned char ide_wait, ide_click;   /* the end shown: a key or a click goes back */
extern char vb_errmsg[];

/* main RAM's [_ide_s0, _ide_s1) and the three banks ide_es (8191 bytes
   each: the records 12-14 were loaded there) change places, 1 KB at a
   time through main RAM's room for names (free: no program is loaded) */
#define XCH 1024
static void ide_swap(void)
{
    unsigned char *t = (unsigned char *)dynbase;
    unsigned int off, n, o, e;
    unsigned char i;
    for (i = 0, off = 0; off < ide_ss; ++i)
        for (o = 0, e = off + 8191; off < e && off < ide_ss; off += n, o += n) {
            n = ide_ss - off;
            if (n > e - off)
                n = e - off;
            if (n > XCH)
                n = XCH;
            memcpy(t, ide_s0 + off, n);
            BankCopy((void *)(0xA000 + o), ide_s0 + off, n, ide_es[i], 0);
            BankCopy(t, (void *)(0xA000 + o), n, 0, ide_es[i]);
        }
}

/* the screen in the run ide_sb: 480 rows of 80 bytes, then 60 rows of
   80 cards' colours; main RAM's room for names the bounce buffer (no
   program is loaded while this runs) */
#define SCR_N 12                        /* (rows a piece: 960 bytes) */
#define SCR_KEEP 10                     /* banks left for the program (its records, stack, heap) */
static void scr_xfer(unsigned long at, unsigned int n, unsigned char out)
{
    unsigned char *m = (unsigned char *)dynbase;
    unsigned int k, o;
    while (n) {
        o = (unsigned int)at & 0x1FFF;
        k = 8192 - o;
        if (k > n)
            k = n;
        if (out)
            BankCopy(m, (void *)(0xA000 + o), k, 0, ide_sb + (unsigned char)(at >> 13));
        else
            BankCopy((void *)(0xA000 + o), m, k, ide_sb + (unsigned char)(at >> 13), 0);
        m += k;
        at += k;
        n -= k;
    }
}

static void scr_save(void)
{
    unsigned char *m = (unsigned char *)dynbase;
    unsigned int y, k;
    unsigned char x;
    static unsigned char sum[TASK_SUM];
    ide_sb = 0;
    TaskCtl(TASK_INFO, 0xFF, sum);      /* (the program first: 6 banks for the */
    if (sum[TS_FREE] < 6 + SCR_KEEP)    /* screen only with SCR_KEEP left over) */
        return;
    ide_sb = BankAlloc(6);
    if (!ide_sb)
        return;                         /* (no room: the editor draws itself again) */
    dispBufferOn = ST_WR_FORE;
    for (y = 0; y < 480; y += SCR_N) {
        for (k = 0; k < SCR_N; ++k)
            ReadRow(m + k * 80, 0, 80, y + k);
        scr_xfer((unsigned long)y * 80, SCR_N * 80, 1);
    }
    for (y = 0; y < 60; y += SCR_N) {
        for (k = 0; k < SCR_N && y + k < 60; ++k)
            for (x = 0; x < 80; ++x)
                m[k * 80 + x] = GetCardColor(x, y + k);
        scr_xfer(38400UL + y * 80, k * 80, 1);
    }
}

/* the editor's screen back: 1 if it was there */
static unsigned char scr_back(void)
{
    unsigned char *m = (unsigned char *)dynbase;
    unsigned int y, k;
    if (!ide_sb)
        return 0;
    dispBufferOn = ST_WR_FORE | ST_WR_BACK;
    clipOn = 0;
    theme_palette();
    for (y = 0; y < 480; y += SCR_N) {
        scr_xfer((unsigned long)y * 80, SCR_N * 80, 0);
        for (k = 0; k < SCR_N; ++k)
            WriteRow(m + k * 80, 0, 80, y + k);
    }
    if (theme_map_on())
        for (y = 0; y < 60; y += SCR_N) {
            k = 60 - y > SCR_N ? SCR_N : 60 - y;
            scr_xfer(38400UL + y * 80, k * 80, 0);
            r0 = (unsigned int)m;
            ColorRect(0, 80, y, k, COLOR_BUF);
        }
    BankFree(ide_sb);
    ide_sb = 0;
    return 1;
}

static void ide_back(void)
{
    ide_req = scr_back();               /* (LOW: 1 the screen is the editor's as it was) */
    asm("sei");
    intTopVector = 0;
    asm("cli");
    keyVector = 0;
    otherPressVec = 0;
    ide_swap();
    ((void (*)(void))ide_s0)();         /* the editor's part: jmp ide_enter */
}

static void ide_clicked(void)
{
    if (!(mouseData & 0x80))
        ide_click = 1;
}

/* the console stays until a key or a click (vb_idle goes back then) */
static void ide_endwait(void)
{
    status("The program has ended.", "A key or a click: back to the editor");
    ide_wait = 1;
    ide_click = 0;
    kqh = kqt;
    otherPressVec = VR_FN(ide_clicked);
}

/* the text checked (vt_src: the editor's), and run */
void ide_go(void)
{
    unsigned char r = ide_req;
    ide_req = IR_NONE;
    ide_swap();
    asm("sei");
    intTopVector = VR_FN(vx_irq);
    asm("cli");
    keyVector = VR_FN(ev_keyv);
    otherPressVec = VR_FN(ev_press);
    appMain = VR_FN(vb_slice);
    ide_wait = 0;
    ended = 1;
    if (r == IR_OUTPUT) {               /* the last program's console again */
        scr_save();
        dispBufferOn = ST_WR_FORE | ST_WR_BACK;
        theme_palette();
        theme_menus();
        DoMenu((void *)&mbar, 0);
        vx_hide = 0;
        vr_win(&win, title);
        memset(cdirty, 1, CON_H);
        con_flush();
        ide_res = 0xFF;                 /* (nothing to say) */
        vb_state = 2;
        ide_endwait();
        return;
    }
    vt_inmem = 1;
    vb_compile();
    if (vt_err) {
        ide_res = vt_err == 1 ? IA_ERR : IA_LOAD;
        if (vt_err == 3)
            strcpy(vt_msg, "Not enough memory (RAM banks).");
        else if (vt_err != 1)
            strcpy(vt_msg, "The program is too big (64 KB of text at most).");
        ide_back();
        return;
    }
    if (r != IR_RUN) {                  /* Check; Format keeps records 0 and 1 */
        if (r == IR_CHECK)
            runs_free();
        ide_res = IA_OK;
        ide_back();
        return;
    }
    scr_save();
    srcmode = 1;
    dispBufferOn = ST_WR_FORE | ST_WR_BACK;
    clipOn = 0;
    theme_palette();
    theme_menus();
    DoMenu((void *)&mbar, 0);
    strcpy(title, "VeraBASIC: ");
    strcat(title, docname);
    vr_win(&win, title);
    vx_reset();
    con_reset();
    con_flush();
    ended = 0;
    if (load(docname))
        status(docname, "Running (Ctrl+C stops it)");
    else {
        strcpy(vt_msg, load_error);
        ide_res = IA_LOAD;
        vb_state = 2;
        ended = 1;
        ide_back();
    }
}

#ifdef IDE_TEST
/* the test build (tools/gate_vb_ide.sh): the session IDE.PLY
   (tests/vbasic/ide_script.py) into a bank, tests/edit_play.s plays it */
extern unsigned char play_bank;
void play_go(void);
static void ide_test_start(void)
{
    unsigned char h, b = BankAlloc(1);
    h = FOpen("IDE.PLY", 'r');
    if (h < 2 || !b) {
        LogText("IDE NO SCRIPT");
        return;
    }
    vb_fbank = b;
    vb_faddr = 0xA000;
    vb_fcount = 8192;
    vb_fread(h);
    FClose(h);
    play_bank = b;
    play_go();
}
#endif
#endif

#ifdef VB_TEST
/* ---- the test build ---- */
#define EMU_OUT (*(volatile unsigned char *)0x9FBB)
static unsigned int lpos, llen;     /* the list: tbank at $B000 */
static char lbuf[80];

/* list byte i */
static char lb(unsigned int i)
{
    static unsigned int at = 0xFFFF;
    if (i < at || i >= at + 64) {
        at = i;
        BankCopy((void *)(0xB000 + at), lbuf, 64, tbank, 0);
    }
    return lbuf[i - at];
}
#define list_at(i) lb(i)
static unsigned int stop_at, aside_at;
static unsigned char key_n, key_c, key_m;   /* !key N CODE MODS: after N handlers */
static unsigned char shot, docmode;

static void out(const char *s)
{
    while (*s)
        EMU_OUT = *s++;
}

static void outn(unsigned int n)
{
    char b[6];
    unsigned char i = 5;
    b[5] = 0;
    do {
        b[--i] = '0' + n % 10;
        n /= 10;
    } while (n);
    out(b + i);
}

static unsigned int num(void)
{
    unsigned int n = 0;
    while (lb(lpos) == ' ')
        ++lpos;
    while (lb(lpos) >= '0' && lb(lpos) <= '9')
        n = n * 10 + (lb(lpos++) - '0');
    while (lb(lpos) == ' ')
        ++lpos;
    return n;
}

static unsigned char kk;
static void kbdbuf_put(unsigned char k)
{
    kk = k;
    asm("lda %v", kk);
    asm("jsr $FEC3");
}

static void the_end(void)
{
    out("VB DONE\n");
    if (shot)
        *(volatile unsigned char *)0x9FB4 = 1;
    asm("jmp $FFFF");
}

static void banks_log(const char *what);

/* !tok NAME.BAS: the tokenizer alone; its four records in hex (G3,
   vbprep.s dump) */
static void tok_dump(const char *name)
{
    out("VB TOK ");
    out(name);
    out("\n");
    if (compile(name))
        vb_dump();
    else {
        out("VB TOKERR ");
        out(load_error);
        out("\n");
    }
    runs_free();
}

/* the next program of the list */
static void next_test(void)
{
    char name[70];
    unsigned char i, h;
    unsigned int n;
    for (;;) {
        if (lpos >= llen)
            the_end();
        stop_at = aside_at = 0;
        vb_tiny = 0;
        if (lb(lpos) == '!') {
            ++lpos;
            if (lb(lpos) == 't' && lb(lpos + 1) == 'i') {          /* !tiny: one statement a slice */
                lpos += 4;
                vb_tiny = 1;
                num();
            } else
            if (lb(lpos) == 's' && lb(lpos + 1) == 'h') {
                shot = 1;
                while (lpos < llen && lb(lpos) != '\n')
                    ++lpos;
                ++lpos;
                continue;
            }
            else if (lb(lpos) == 't' && lb(lpos + 1) == 'o') {     /* !tok NAME.BAS */
                lpos += 4;
                while (lb(lpos) == ' ')
                    ++lpos;
                i = 0;
                while (lpos < llen && lb(lpos) != '\n' && i < 63)
                    name[i++] = lb(lpos++);
                name[i] = 0;
                ++lpos;
                tok_dump(name);
                continue;
            }
            else if (lb(lpos) == 'k') {                            /* !key n code mods */
                lpos += 3;
                key_n = num() + 1;
                key_c = num();
                key_m = num();
                ++lpos;
                continue;
            }
            else if (lb(lpos) == 'f') {                            /* !fill n */
                lpos += 4;
                vb_tfill(num());
                ++lpos;
                continue;
            }
            else if (lb(lpos) == 's') {
                lpos += 4;
                stop_at = num();
            } else if (lb(lpos) == 'a') {
                lpos += 5;
                aside_at = num();
            }
        }
        i = 0;
        while (lpos < llen && lb(lpos) != '\n' && i < 63)
            name[i++] = lb(lpos++);
        name[i] = 0;
        ++lpos;
        if (!i)
            continue;
        break;
    }
    /* the top folder (a program before may have changed it: CHDIR, the
       Open dialog; the emulator echoes the DOS's command: a line of its
       own); its keys */
    if (curDirName[0]) {
        ChDir("/");
        out("\n");
        curDirName[0] = 0;
    }
    strcpy(docname, name);
    strcat(docname, ".KEY");
    scr_ptr = scr_end = (unsigned char *)0xA000;
    h = FOpen(docname, 'r');
    if (h >= 2) {
        vb_fbank = tbank;
        vb_faddr = 0xA000;
        vb_fcount = 4096;
        n = vb_fread(h);
        FClose(h);
        scr_end = (unsigned char *)0xA000 + n;
    }
    kqh = kqt = 0;
    out("VB RUN ");
    out(name);
    out("\n");
    vb_slices = 0;
    start(name);
    banks_log("VB BANKS ");
}

/* the RAM banks free (and there for programs), from the switcher's summary */
static unsigned char sum[TASK_SUM];
static void banks_log(const char *what)
{
    TaskCtl(TASK_INFO, 0xFF, sum);
    out(what);
    outn(sum[TS_FREE]);
    out(" FREE OF ");
    outn(sum[TS_BANKS]);
    out("\n");
}

static void test_main(void)
{
    vb_slice();
    if (vb_state == 1) {
        if (stop_at && vb_slices >= stop_at) {
            stop_at = 0;
            out("VB CTRL+C\n");
            kbdbuf_put(3);
        }
        if (key_n && vx_idle && vx_evn >= key_n - 1) {
            key_n = 0;                  /* (as the kernel calls keyVector) */
            keyData = key_c;
            keyDataMods = key_m;
            ev_keyv();
        }
        if (aside_at && vb_slices >= aside_at) {
            aside_at = 0;
            out("VB ASIDE\n");
            TaskCtl(TASK_ASIDE, 0, 0);
        }
    }
}
#endif

/* appMain while no program runs: the console drawn, the end shown */
void vb_idle(void)
{
#ifndef VB_IDE
    static char line[80];
#endif
    con_flush();
#ifdef VB_IDE
    edit_idle = 1;                      /* (the test's player) */
    if (vb_state != 2)
        return;
    if (ended) {
        if (ide_wait && (kqh != kqt || ide_click)) {
            ide_wait = 0;
            ide_back();
        }
        return;
    }
    ended = 1;
    free_program();
    if (vb_how == 1) {                  /* an error, or Ctrl+C: its line (vb_esrc) */
        ide_res = vb_errno == 1 ? IA_STOP : IA_RTERR;
        strcpy(vt_msg, vb_errmsg);
        ide_back();
    } else if (vb_how == 3) {
        ide_res = IA_LOAD;
        strcpy(vt_msg, load_error);
        ide_back();
    } else {
        ide_res = IA_END;
        if (vx_used | vx_hide)          /* a program of windows: straight back */
            ide_back();
        else
            ide_endwait();
    }
#else
    if (vb_state != 2 || ended)
        return;
    ended = 1;
#ifdef VB_TEST
    status(vb_how == 1 ? "The program stopped with an error." : "The program has ended.", "");
    if (vb_how == 1) {
        out("VB ERROR ");
        outn(vb_errno);
        out("\n");
    } else if (vb_how == 2)
        out("VB INPUT\n");
    else if (vb_how == 3)
        out("VB LOAD\n");
    else
        out("VB END\n");
    banks_log("VB BANKS END ");
    out("VB GC ");
    outn(gc_count);
    out(" IDLE ");
    outn((unsigned int)(vb_idlecyc >> 10));
    out("K\n");
    vb_idlecyc = 0;
    free_program();
    if (docmode) {                      /* back to the shell */
        out("VB DONE\n");
        EnterDeskTop();
    }
    next_test();
#else
    free_program();
    if (vb_how == 0 && vx_used) {       /* a windowed program's END: it quits */
        EnterDeskTop();
    }
    if (vb_how == 1) {
        status("The program stopped with an error.", "");
        PlaySound(SND_ERROR);
        strcpy(line, docname);
        message(line, "stopped with an error: see the console.");
        EnterDeskTop();
    }
    if (vb_how == 3) {                  /* not loaded: an error in its text, or the file */
        PlaySound(SND_ERROR);
        status(load_error, "Quit: VeraBASIC Run menu");
        strcpy(line, docname);
        message(line, load_error);
        return;
    }
    status("The program has ended.", "Quit: VeraBASIC Run menu");
#endif
#endif
}

#ifndef VB_IDE
static void open_doc(void)
{
    /* ST_LD_DATA: the document's whole name */
    unsigned char i;
    for (i = 0; i < 63 && dataLongName[i]; ++i)
        docname[i] = dataLongName[i];
    docname[i] = 0;
    if (curDirName[0])
        ChDir((char *)curDirName);
    start(docname);
}
#endif

/* the other overlays' banks (bcall maps them): the tokenizer 2-5, the
   Veranda extensions 6-11 (loaded from here: main RAM is the program's) */
static unsigned char ovl(unsigned char n)
{
    LoadOverlay(n, 0);
    return r1L;
}

/* the start (overlay 1): the window, the menus, the document */
static void init(unsigned char opts)
{
    vt_ovl2 = ovl(2);
    vt_ovl3 = ovl(3);
    vt_ovl4 = ovl(4);
    vt_ovl5 = ovl(5);
    vx_ovl6 = ovl(6);
    vx_ovl7 = ovl(7);
    vx_ovl8 = ovl(8);
    vx_ovl9 = ovl(9);
    vx_ovla = ovl(10);
    vx_ovlb = ovl(11);
    dynbase = (unsigned int)_BSS_RUN__ + (unsigned int)_BSS_SIZE__;
    vb_fontload();
    cbank = BankAlloc(1);
#ifndef VB_IDE
    tbank = BankAlloc(1);               /* (the tests' lists and keys) */
#endif
    dispBufferOn = ST_WR_FORE | ST_WR_BACK;
    SetPattern(0);
    WRectangle(0, 0, 639, 479);
    theme_load();
    theme_palette();
#ifdef VB_IDE
    {
        /* the editor: its part's records (3) and banks; the interpreter's
           code gives it its place (ide_back) */
        unsigned char i;
        ide_opts = opts;
        ide_ovl1 = appBank;
        ide_govec = VR_FN(ide_go);
        for (i = 0; i < 3; ++i)
            ide_es[i] = ovl(IDE_REC_LOW + i);
        for (i = 0; i < IDE_NBANKS; ++i)
            ide_banks[i] = ovl(IDE_REC_BANK + i);
        ide_ss = ide_s1 - ide_s0;
        vb_state = 2;
        ended = 1;
        strcpy(title, "VeraBASIC");
#ifdef IDE_TEST
        vb_test = 1;
        ide_test_start();
#endif
        ide_back();
        return;
    }
#endif
    vr_win(&win, "VeraBASIC Run");
    theme_menus();
    DoMenu((void *)&mbar, 0);
    con_reset();
    RAMBANK = appBank;
    keyVector = VR_FN(ev_keyv);         /* (vbev.s: shortcuts, ON KEY, else vb_key) */
    otherPressVec = VR_FN(ev_press);
    appMain = VR_FN(vb_slice);
    asm("sei");
    intTopVector = VR_FN(vx_irq);
    asm("cli");       /* PLAY's sequencer (the test: and vb_wd) */
#ifdef VB_TEST
    vb_test = 1;
    {
        unsigned char h = FOpen("VBTEST.LST", 'r');
        if (h >= 2) {
            vb_fbank = tbank;
            vb_faddr = 0xB000;
            vb_fcount = 4096;
            llen = vb_fread(h);
            FClose(h);
        }
    }
    LogText("VB TEST");
    appMain = VR_FN(test_main);
    if (opts & ST_LD_DATA) {            /* started by the shell with a document */
        docmode = 1;
        scr_ptr = scr_end = (unsigned char *)0xA000;
        out("VB RUN ");
        out((const char *)dataLongName);
        out("\n");
        open_doc();
        return;
    }
    next_test();
    return;
#endif
#ifndef VB_IDE
    if (opts & ST_LD_DATA)
        open_doc();
    else {
        status("Open a VeraBASIC program: VeraBASIC Run menu, Open program...", "");
        vb_state = 2;
        ended = 1;
        m_open();
    }
#endif
    }

#pragma code-name (pop)

int main(void)
{
    unsigned char opts = r0L;
    LoadOverlay(1, 0);                  /* overlay 1: mapped whenever the kernel calls us */
    appBank = r1L;
    RAMBANK = appBank;
    init(opts);
    return 0;
}
