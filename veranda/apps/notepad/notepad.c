/* Note Pad: a Veranda desk accessory in C, GEOS's note pad again.

   A window of notes over the application that opened it: pages of up to
   640 characters (36 columns, 18 rows, wrapped at the window's edge; the
   X16 ROM's ISO font through PutCards). The pages are the records of the
   record file "Note File" in /VERANDA/.SYSTEM (DECISIONS V71; the root
   without it): one page a record, up to 127. Its window: the close box
   and "page n of m" at the top; the text; at the bottom Prev, Next, New
   and Delete. Keys: typing, Return, Backspace, Delete, the arrows; PgUp
   and PgDn turn the pages (PgDn on the last page makes a new one unless
   it is empty); Ctrl+N a new page after this one, Ctrl+D deletes this
   page; Escape closes it. A page is written back when it is turned or the
   pad closed, if it changed; the file is closed before RstrAppl.

   MIT licence (see LICENSE). */
#include <veranda.h>
#include <vr.h>
#include <vrpath.h>
#include <string.h>
#include <theme.h>

#define X0      160             /* the window: card aligned */
#define Y0      136
#define W       320
#define H       208
#define TX      (X0 / 8 + 2)    /* the text: card column, top row */
#define TY      (Y0 + 24)
#define COLS    36
#define ROWS    18
#define MAXP    640
#define BY      (Y0 + H - 26)   /* the buttons */
#define BH      18

static const char fname[] = "Note File";
static unsigned char font[2048];
static char page[MAXP + 1];
static unsigned int len, cur;
static unsigned char pg, npages, dirty;
static unsigned int rowst[ROWS + 2];    /* each row's first character */
static unsigned char nrows;
static char line[48];

struct button { unsigned int l, r; const char *t; };
static const struct button buttons[4] = {
    { X0 + 12, X0 + 76, "< Prev" },
    { X0 + 84, X0 + 148, "Next >" },
    { X0 + 168, X0 + 224, "New" },
    { X0 + 232, X0 + 304, "Delete" },
};

#ifdef NOTE_TEST
void note_test_start(void);
#endif

static void log2(const char *a, unsigned char x, unsigned char y)
{
    char *p = line;
    while (*a)
        *p++ = *a++;
    *p++ = ' ';
    if (x >= 100)
        *p++ = '0' + x / 100;
    if (x >= 10)
        *p++ = '0' + x / 10 % 10;
    *p++ = '0' + x % 10;
    *p++ = ' ';
    if (y >= 100)
        *p++ = '0' + y / 100;
    if (y >= 10)
        *p++ = '0' + y / 10 % 10;
    *p++ = '0' + y % 10;
    *p = 0;
    LogText(line);
}

/*---------------------------------------------------------------
  the font: the ROM's ISO set (bank 6), as Veranda Edit finds it
 ---------------------------------------------------------------*/
static const unsigned char sigA[8] = { 0x18, 0x3C, 0x24, 0x66, 0x7E, 0x66, 0x66, 0x00 };

static void font_load(void)
{
    unsigned char keep;
    unsigned int a;
    asm("sei");
    keep = *(unsigned char *)1;
    *(unsigned char *)1 = 6;
    for (a = 0xC000; a; a += 0x800)
        if (!memcmp((void *)(a + 0x41 * 8), sigA, 8) && *(unsigned char *)a == 0xC3)
            break;
    memcpy(font, (void *)(a ? a : 0xC800), 2048);
    *(unsigned char *)1 = keep;
    asm("cli");
}

/*---------------------------------------------------------------
  the file
 ---------------------------------------------------------------*/
/* back to the application's folder: curDirName, the path applications
   keep (DECISIONS V48); empty, the root */
static void app_dir(void)
{
    ChDir(curDirName[0] == '/' ? (char *)curDirName : "/");
}

static void sys_dir(void)
{
    vr_cd(VR_DATA);                     /* /VERANDA/.SYSTEM (DECISIONS V71) */
}

/* a new, empty Note File (a record file of type APPL_DATA) */
static unsigned char create(void)
{
    static unsigned char hdr[256];
    static const unsigned char icon[63] = {
        0x15, 0x55, 0x40, 0x3F, 0xFF, 0xF0, 0x20, 0x00, 0x10, 0x2F, 0xFF, 0xD0,
        0x20, 0x00, 0x10, 0x2F, 0xFF, 0xD0, 0x20, 0x00, 0x10, 0x2F, 0xFF, 0xD0,
        0x20, 0x00, 0x10, 0x2F, 0xFF, 0xD0, 0x20, 0x00, 0x10, 0x2F, 0xFC, 0x10,
        0x20, 0x00, 0x10, 0x20, 0x00, 0xF0, 0x20, 0x00, 0xA0, 0x20, 0x00, 0xC0,
        0x3F, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00
    };
    memset(hdr, 0, sizeof hdr);
    hdr[0] = (unsigned int)fname & 255;
    hdr[1] = (unsigned int)fname >> 8;
    hdr[2] = 3;
    hdr[3] = 21;
    hdr[4] = 0xBF;
    memcpy(hdr + 5, icon, 63);
    hdr[68] = 0x83;
    hdr[69] = APPL_DATA;
    hdr[70] = 1;
    memcpy(hdr + O_GHFNAME, "Note File   V1.0", 16);
    memcpy(hdr + O_GH_AUTHOR, "Veranda", 7);
    memcpy(hdr + O_GHP_FNAME, "Note Pad    V1.0", 16);
    memcpy(hdr + O_GHINFO_TXT, "The pages of the Note Pad.", 26);
    return SaveFile(hdr, 0);
}

static void load(void)
{
    len = 0;
    if (!PointRecord(pg) && !ReadRecord(page, MAXP))
        len = r7 - (unsigned int)page;
    if (len > MAXP)
        len = MAXP;
    page[len] = 0;
    cur = len;
    dirty = 0;
}

static void save(void)
{
    if (!dirty)
        return;
    PointRecord(pg);
    WriteRecord(page, len);
    dirty = 0;
}

/*---------------------------------------------------------------
  drawing
 ---------------------------------------------------------------*/
/* the rows: wrapped at COLS, a LF ends one; 0 if the text does not fit */
static unsigned char layout(void)
{
    unsigned int i = 0, c = 0;
    nrows = 0;
    rowst[0] = 0;
    for (i = 0; i < len; ++i) {
        if (page[i] == 10 || c == COLS) {
            if (++nrows >= ROWS)
                return 0;
            rowst[nrows] = page[i] == 10 ? i + 1 : i;
            c = page[i] == 10 ? 0 : 1;
        } else
            ++c;
    }
    rowst[nrows + 1] = len + 1;
    return 1;
}

static unsigned char row_of(unsigned int p, unsigned char *col);

static void draw_text(void)
{
    unsigned char r, k, cx, cy;
    unsigned int s, e;
    for (r = 0; r < ROWS; ++r) {
        memset(line, ' ', COLS);
        if (r <= nrows) {
            s = rowst[r];
            e = r < nrows ? rowst[r + 1] : len;
            for (k = 0; s + k < e && k < COLS && page[s + k] != 10; ++k)
                line[k] = page[s + k] ? page[s + k] : ' ';
        }
        PutCards(line, font, TX, COLS, TY + r * 8, 0);
    }
    /* the cursor: a bar at the left of its cell */
    cy = row_of(cur, &cx);
    WInvertRectangle(TX * 8 + cx * 8, TY + cy * 8, TX * 8 + cx * 8 + 1, TY + cy * 8 + 7);
}

static void text(const char *s, unsigned int x, unsigned int y, unsigned char bold)
{
    line[0] = bold ? BOLDON : PLAINTEXT;
    strcpy(line + 1, s);
    WPutString(line, x, y);
}

static void draw_title(void)
{
    char *p = line + 20;
    SetPattern(0);
    WRectangle(X0 + 200, Y0 + 3, X0 + W - 3, Y0 + 15);
    strcpy(p, "page ");
    p += 5;
    if (pg + 1 >= 100)
        *p++ = '0' + (pg + 1) / 100;
    if (pg + 1 >= 10)
        *p++ = '0' + (pg + 1) / 10 % 10;
    *p++ = '0' + (pg + 1) % 10;
    strcpy(p, " of ");
    p += 4;
    if (npages >= 100)
        *p++ = '0' + npages / 100;
    if (npages >= 10)
        *p++ = '0' + npages / 10 % 10;
    *p++ = '0' + npages % 10;
    *p = 0;
    memmove(line + 1, line + 20, p - (line + 20) + 1);
    line[0] = PLAINTEXT;
    WPutString(line, X0 + 220, Y0 + 12);
}

static void draw_page(void)
{
    layout();
    draw_text();
    draw_title();
}

static void frame(void)
{
    unsigned char i;
    SetPattern(0);
    WRectangle(X0, Y0, X0 + W - 1, Y0 + H - 1);
    WFrameRectangle(0xFF, X0, Y0, X0 + W - 1, Y0 + H - 1);
    WFrameRectangle(0xFF, X0 + 1, Y0 + 1, X0 + W - 2, Y0 + H - 2);
    SetPattern(2);                      /* the title bar */
    WRectangle(X0 + 2, Y0 + 2, X0 + W - 3, Y0 + 16);
    SetPattern(0);
    WRectangle(X0 + 4, Y0 + 4, X0 + 14, Y0 + 14);      /* the close box */
    WFrameRectangle(0xFF, X0 + 4, Y0 + 4, X0 + 14, Y0 + 14);
    WRectangle(X0 + 24, Y0 + 3, X0 + 90, Y0 + 15);
    text("Note Pad", X0 + 28, Y0 + 12, 1);
    WHorizontalLine(0xFF, X0 + 2, X0 + W - 3, Y0 + 17);
    WHorizontalLine(0xFF, X0 + 2, X0 + W - 3, BY - 6);
    for (i = 0; i < 4; ++i) {
        WFrameRectangle(0xFF, buttons[i].l, BY, buttons[i].r, BY + BH);
        text(buttons[i].t, buttons[i].l + 8, BY + 12, 0);
    }
}

/*---------------------------------------------------------------
  pages
 ---------------------------------------------------------------*/
static void turn(unsigned char to)
{
    save();
    pg = to;
    load();
    draw_page();
    log2("NOTE PAGE", pg + 1, npages);
}

static void next(void)
{
    if (pg + 1 < npages) {
        turn(pg + 1);
        return;
    }
    if (!len || npages >= 127) {
        Beep();
        return;
    }
    save();
    PointRecord(pg);
    AppendRecord();
    ++npages;
    turn(pg + 1);
}

static void prev(void)
{
    if (pg)
        turn(pg - 1);
    else
        Beep();
}

static void newpage(void)
{
    if (npages >= 127) {
        Beep();
        return;
    }
    save();
    PointRecord(pg);
    AppendRecord();
    ++npages;
    turn(pg + 1);
}

static void delpage(void)
{
    dirty = 0;
    PointRecord(pg);
    DeleteRecord();
    if (!--npages) {
        AppendRecord();
        npages = 1;
    }
    if (pg >= npages)
        pg = npages - 1;
    turn(pg);
}

static void quit(void)
{
    save();
    CloseRecordFile();
    app_dir();
    LogText("NOTE CLOSED");
    RstrAppl();
}

/*---------------------------------------------------------------
  editing
 ---------------------------------------------------------------*/
static void insert(char c)
{
    if (len >= MAXP)
        goto full;
    memmove(page + cur + 1, page + cur, len - cur + 1);
    page[cur] = c;
    ++len;
    if (!layout()) {
        memmove(page + cur, page + cur + 1, len - cur);
        --len;
        layout();
        goto full;
    }
    ++cur;
    dirty = 1;
    draw_text();
    return;
full:
    Beep();
}

static void erase(unsigned int at)
{
    if (at >= len)
        return;
    memmove(page + at, page + at + 1, len - at);
    --len;
    cur = at;
    dirty = 1;
    layout();
    draw_text();
}

/* the row of position p, and its column */
static unsigned char row_of(unsigned int p, unsigned char *col)
{
    unsigned char r = 0;
    while (r < nrows && rowst[r + 1] <= p)
        ++r;
    *col = p - rowst[r];
    return r;
}

static void vmove(signed char d)
{
    unsigned char col, r = row_of(cur, &col);
    unsigned int e;
    if ((d < 0 && !r) || (d > 0 && r >= nrows))
        return;
    r += d;
    e = r < nrows ? rowst[r + 1] - 1 : len;
    cur = rowst[r] + col;
    if (cur > e)
        cur = e;
    draw_text();
}

static void key(void)
{
    unsigned char k = keyData, m = keyDataMods;
    if (m & KEYM_ARROW) {
        if (k == KEY_LEFT && cur)
            --cur;
        else if (k == KEY_RIGHT && cur < len)
            ++cur;
        else if (k == KEY_UP)
            vmove(-1);
        else if (k == KEY_DOWN)
            vmove(1);
        draw_text();
        return;
    }
    if (m & 2) {                        /* Ctrl + a letter */
        if (k == 14)
            newpage();
        else if (k == 4)
            delpage();
        return;
    }
    switch (k) {
    case KEY_ESC: quit(); return;
    case KEY_PGDN: next(); return;
    case KEY_PGUP: prev(); return;
    case 8: if (cur) erase(cur - 1); return;
    case KEY_DELETE: erase(cur); return;
    case CR: insert(10); return;
    }
    if ((k >= 32 && k < 127) || k >= 160)
        insert(k);
}

static void press(void)
{
    unsigned int x, y;
    unsigned char i, r, c;
    if (mouseData & 0x80)
        return;
    do {
        x = mouseXPos;
        y = mouseYPosW;
    } while (x != mouseXPos || y != mouseYPosW);
    if (x >= X0 + 4 && x <= X0 + 14 && y >= Y0 + 4 && y <= Y0 + 14) {
        quit();
        return;
    }
    if (y >= BY && y <= BY + BH) {
        for (i = 0; i < 4; ++i)
            if (x >= buttons[i].l && x <= buttons[i].r)
                switch (i) {
                case 0: prev(); return;
                case 1: next(); return;
                case 2: newpage(); return;
                case 3: delpage(); return;
                }
        return;
    }
    if (x >= TX * 8 && x < (TX + COLS) * 8 && y >= TY && y < TY + ROWS * 8) {
        r = (y - TY) >> 3;
        c = (x - TX * 8 + 3) >> 3;
        if (r > nrows)
            r = nrows;
        cur = rowst[r] + c;
        if (r < nrows && cur >= rowst[r + 1])
            cur = rowst[r + 1] - 1;
        if (cur > len)
            cur = len;
        draw_text();
    }
}

static void tick(void)
{
    unsigned int x = mouseXPos, y = mouseYPosW;
    SetPointer(x >= TX * 8 && x < (TX + COLS) * 8 && y >= TY && y < TY + ROWS * 8 ? PTR_IBEAM : PTR_ARROW);
}

int main(void)
{
    theme_load();
    theme_menus();                      /* (V79: its boxes in the UI font) */
    font_load();
    dispBufferOn = ST_WR_FORE;
    frame();
    sys_dir();
    if (OpenRecordFile((char *)fname)) {
        if (create() || OpenRecordFile((char *)fname)) {
            LogText("NOTE NO FILE");
            app_dir();
            RstrAppl();
        }
    }
    npages = usedRecords;
    if (!npages) {
        AppendRecord();
        npages = 1;
    }
    pg = 0;
    load();
    draw_page();
    keyVector = VR_FN(key);
    otherPressVec = VR_FN(press);
    appMain = VR_FN(tick);
    log2("NOTE RUNNING", pg + 1, npages);
#ifdef NOTE_TEST
    note_test_start();
#endif
    return 0;
}
