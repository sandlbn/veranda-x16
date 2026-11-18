/* gate term: Veranda Term's test build (included at the end of term.c
   with TERM_TEST; tests/term_play.s plays the mouse and the keys).

   TERM.TST, one line: "<session> <stop> <baud index> <time zone>", e.g.
   "bbs end 4 4". Sessions (tools/gate_term.sh, tests/term_bbs.py):
     page    the script back-end: TERM.IN played at the baud rate, captured
             to CAPPAGE.TXT; the model at the end in TERM.SCR
     bbs     dial "bbs.test:23" through the menu and the dial dialog; the
             ANSI page; "hello veranda" typed and echoed; file/about over
             the coloured screen; stop: menu (a menu open), dlg (the box
             open), back (PgUp: the scrollback), end
     speed   capture to SPEED.TXT, dial speed.test:23, until NO CARRIER;
             stop "aside" (V91): after 500 bytes TaskCtl(TASK_ASIDE): the
             kernel calls asideVector (the port's interrupts off, "TERM
             ASIDE"), the test desktop (tests/term_desk.s) waits 3 s while
             the BBS sends and brings Term back ("TERM BACK")
     xmodem  dial files.test:23, two downloads (DL1.BIN, DL2.BIN)
     clock   connect/clock from network, the box's YES clicked
     dex     started by Dex's Connect with DEX.TEL (gate da): it dials the
             document's host; the end once online (or 2 s without a UART)
   The end: TERM.SCR written, a line logged, VRAM dumped. */

void __fastcall__ term_play(const unsigned int *script);
void term_dump(void);

static unsigned int scr[3 * 48];
static unsigned char ns;
static char tst[40];
static unsigned char tstep, tstop;
static unsigned int tt, tidle, tbase;
static unsigned long tlast;
static unsigned int tfirst, tend;
static unsigned long tbytes;

#define F_BTN 0x100
#define F_KEY 0x8000
#define F_END 0x2000
#define F_WAIT 0x1000

static void S(unsigned int f, unsigned int x, unsigned int y, unsigned int fl)
{
    scr[ns++] = f;
    scr[ns++] = x;
    scr[ns++] = (y & 255) | (y > 255 ? 0x4000 : 0) | fl;
}


/* a click (down, up 4 frames later) */
static void C(unsigned int f, unsigned int x, unsigned int y)
{
    S(f, x, y, 0);
    S(f + 3, x, y, F_BTN);
    S(f + 7, x, y, 0);
}

/* a menu of the bar (x) opened, its item n chosen (ix: inside the sub
   menu): the pointer goes into the sub menu at once, as a hand does
   (outside it the menu closes) */
static void M(unsigned int f, unsigned int x, unsigned int ix, unsigned char n)
{
    S(f, x, 7, 0);
    S(f + 1, x, 7, F_BTN);
    S(f + 2, ix, 24, 0);
    C(f + 20, ix, 24 + 16 * n);         /* (the Veranda look's lists: 16 lines an item, V74) */
}

static unsigned int typed(unsigned int f, const char *s)
{
    while (*s) {
        S(f, *s++, 0, F_KEY);
        f += 3;
    }
    return f;
}

static void go(void)
{
    scr[ns++] = 0xFFFF;
    scr[ns++] = 0xFFFF;
    scr[ns++] = 0;
    term_play(scr);
    ns = 0;
}

static void snap(void)
{
    unsigned char h, r;
    h = FOpen("TERM.SCR", 'w');
    for (r = 0; r < ROWS; ++r) {
        vt_cell_text(r, rxbuf);
        vt_cell_attr(r, rxbuf + COLS);
        FWrite(h, rxbuf, 2 * COLS);
    }
    rxbuf[0] = vt_cx;
    rxbuf[1] = vt_cy;
    rxbuf[2] = tn_seen;
    rxbuf[3] = tn_on;
    rxbuf[4] = tn_echo;
    FWrite(h, rxbuf, 5);
    FClose(h);
}

static char *word(char *p, char *out)
{
    while (*p == ' ')
        ++p;
    while (*p > ' ')
        *out++ = *p++;
    *out = 0;
    return p;
}

static char sess[8], stopw[8], nb[4];

static void test_start(void)
{
    unsigned char h = FOpen("TERM.TST", 'r');
    char *p;
    if (h < 2)
        return;
    tst[FRead(h, tst, sizeof tst - 1)] = 0;
    FClose(h);
    p = word(word(tst, sess), stopw);
    p = word(p, nb);
    if (nb[0])
        cfg.baud = nb[0] - '0';
    p = word(p, nb);
    if (nb[0])
        cfg.tz = nb[0] - '0';
    cfg.backend = sess[0] == 'p';
    tstop = stopw[0];
    tstep = 1;
    tbase = ser_frames;
}

static char logl[64];

static void logn(const char *k, unsigned int v)
{
    char *p = put_str(logl, k);
    put_num(p, v)[0] = 0;
    LogText(logl);
}

static void the_end(void)
{
    capture_stop();
    snap();
#ifdef TERM_CLIP
    copy_screen();                      /* (gate clip: options/copy screen) */
#endif
    logn("TERM LOST ", ser_lost);
    logn("TERM OE ", ser_oe);
    logn("TERM TN ", tn_seen);
    LogText("TERM END");
    term_dump();
}

/* a byte came in during the last frames? */
static unsigned char quiet(unsigned int n)
{
    if (rx_total != tlast) {
        if (!tfirst)
            tfirst = ser_frames;
        tend = ser_frames;
        tlast = rx_total;
        tidle = ser_frames;
    }
    return ser_frames - tidle > n && rx_total;
}

static void test_tick(void)
{
    unsigned char q;
    if (!tstep)
        return;
    tt = ser_frames - tbase;
    q = quiet(120);
    switch (sess[0]) {
    case 'd':                           /* Dex's Connect (gate da): dialled from DEX.TEL */
        if ((online && q) || (nouart && tt >= 120))
            the_end();
        break;
    case 'p':                           /* the script back-end */
        if (tstep == 1) {
            capture_start("CAPPAGE.TXT");
            dial("page", "page");
            tstep = 2;
        } else if (tstep == 2 && !ser_send && q)
            the_end();
        break;
    case 'b':                           /* the BBS */
        if (tstep == 1 && tt >= 20) {
            M(0, barx[2], mconn.left + 12, 0);  /* connect/dial */
            if (tstop == 'y')
                S(75, 0, 0, F_END);
            typed(typed(80, "bbs.test:23"), "\r");
            go();
            tstep = 2;
        } else if (tstep == 2 && online && q) {
            typed(typed(0, "hello veranda"), "\r");
            go();
            tidle = ser_frames;
            tstep = 3;
        } else if (tstep == 3 && q) {
            snap();                     /* (the screen before the menu) */
            if (tstop == 'm') {
                S(0, barx[0], 7, 0);    /* Term, open */
                S(1, barx[0], 7, F_BTN);
                S(2, mterm.left + 12, 24, 0);
                S(40, 0, 0, F_END);
            } else
                M(0, barx[0], mterm.left + 12, 0);      /* Term/about */
            if (tstop == 'd')
                S(100, 0, 0, F_END);
            S(100, 13, 0, F_KEY);       /* Return: OK */
            go();
            tbase = ser_frames;
            tstep = 4;
        } else if (tstep == 4 && tt > 200) {
            snap();
            if (tstop == 'b') {
                S(0, 0x82, 0, F_KEY);   /* PgUp */
                S(60, 0, 0, F_END);
                go();
                tstep = 5;
            } else
                the_end();
        }
        break;
    case 's':                           /* throughput and capture */
        if (tstep == 1) {
            if (tstop != 'a')           /* (a stream open: it would be asked to quit) */
                capture_start("SPEED.TXT");
            dial("speed", "speed.test:23");
            tstep = 2;
        } else if (tstep == 2 && online) {
            tstep = 3;                  /* from CONNECT on: the data */
            tfirst = 0;
            tbytes = rx_total;
        } else if (tstep == 3 && tstop == 'a' && rx_total > 500) {
            tstop = 0;                  /* (stop "aside", V91) */
            TaskCtl(TASK_ASIDE, 0, 0);
        } else if (tstep == 3 && !online && q) {
            logn("TERM BYTES ", rx_total - tbytes);
            logn("TERM FRAMES ", tend - tfirst);
            logn("TERM POLLS ", ser_polls);
            the_end();
        }
        break;
    case 'x':                           /* XMODEM */
        if (tstep == 1) {
            dial("files", "files.test:23");
            tstep = 2;
        } else if (tstep == 2 && online && q) {
            xm_start("DL1.BIN");
            tstep = 3;
        } else if (tstep == 3 && xm_result) {
            logn("TERM XM1 ", xm_result * 1000 + xm_crc * 100 + xm_big * 10);
            logn("TERM XM1 RETRIES ", xm_retries);
            xm_start("DL2.BIN");
            tstep = 4;
        } else if (tstep == 4 && xm_result) {
            logn("TERM XM2 ", xm_result * 1000 + xm_crc * 100 + xm_big * 10);
            logn("TERM XM2 RETRIES ", xm_retries);
            tstep = 5;
        } else if (tstep == 5 && !online && q)
            the_end();
        break;
    case 'c':                           /* the clock */
        if (tstep == 1 && tt >= 20) {
            M(0, barx[2], mconn.left + 12, 4);  /* connect/clock from network */
            S(40, (unsigned int)&test_box, 0, F_WAIT);  /* the box is open */
            if (tstop == 'd')
                S(30, 0, 0, F_END);
            C(30, 256, 272);            /* YES */
            C(200, 256, 272);           /* (again: nothing there now: V40) */
            C(260, 100, 300);           /* (and in the window) */
            go();
            tstep = 2;
            tbase = ser_frames;
        } else if (tstep == 2 && test_boxed) {
            tbase = ser_frames;         /* the box was answered */
            tstep = 4;
        } else if (tstep == 4 && tt > 400) {
            /* Veranda's variables, then the ROM's clock (the RTC) */
            char *p = put_str(logl, "TERM VARS ");
            p = fmt_when(p, year, month, day, hour, minutes, seconds);
            LogText(logl);
            asm("jsr $FF50");           /* clock_get_date_time: r0-r3 */
            p = put_str(logl, "TERM RTC ");
            fmt_when(p, r0L, r0H, r1L, r1H, r2L, r2H);
            LogText(logl);
            the_end();
        }
        break;
    }
}
