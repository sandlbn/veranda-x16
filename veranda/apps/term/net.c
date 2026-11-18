/* Veranda Term: the serial port's C half, Telnet, the modem, XMODEM and
   the clock from the network (MIT licence, see LICENSE).

   The serial driver has two back-ends behind one ring (ser.s): the
   16C550 (programmed here, read and written by the interrupt) and the
   script (bytes from a file in RAM banks, for gates without a port).

   Received bytes go: Telnet (IAC sequences answered and removed) -> the
   capture file -> XMODEM, while a download runs, else the modem's result
   lines (CONNECT, NO CARRIER, OK) are noticed and every byte goes to the
   screen (vt.c). */
#include <string.h>
#include "term.h"
#include "net.h"

/*---------------------------------------------------------------
  the serial port
 ---------------------------------------------------------------*/
#define REG(r)  (*(volatile unsigned char *)(0x9F00 + cfg.port + (r)))
#define VERA_IEN  (*(volatile unsigned char *)0x9F26)
#define VERA_LINE (*(volatile unsigned char *)0x9F28)

unsigned char ser_at(unsigned char lo)
{
    volatile unsigned char *scr = (volatile unsigned char *)(0x9F07 + lo);
    *scr = 0x55;
    if (*scr != 0x55)
        return 0;
    *scr = 0xAA;
    return *scr == 0xAA;
}

/* the card's DIP switches put its first UART (the ESP32's) at one of ten
   16-byte ranges $9F60-$9FF0 (the factory's $9FE0) and the second (the
   DE9 port) 8 above it (V91: TexElec's card) */
unsigned char ser_probe(void)
{
    unsigned char lo = 0x60;
    do {
        if (ser_at(lo))
            return lo;
        lo += 0x10;
    } while (lo);
    return 0;
}

unsigned int ser_divisor(void)
{
    unsigned int b = bauds[cfg.baud], d = (xtals[cfg.xtal] + b / 2) / b;
    return d ? d : 1;
}

static void lines_on(unsigned char on)
{
    asm("sei");
    if (on) {
        VERA_LINE = 120;
        VERA_IEN = (VERA_IEN & 0x7F) | 0x02;
    } else
        VERA_IEN &= 0x7D;
    asm("cli");
}

unsigned char ser_start(void)
{
    unsigned int d;
    ser_stop();
    asm("sei");
    ser_rxhead = ser_rxtail = 0;
    ser_txhead = ser_txtail = 0;
    ser_flow = 0;
    asm("cli");
    if (cfg.backend == 1) {
        ser_on = 2;
        return 0;
    }
    if (!ser_at(cfg.port))
        return 1;
    d = ser_divisor();
    ser_lo = cfg.port;
    REG(1) = 0;                 /* no interrupts while it is set up */
    REG(3) = 0x80;              /* the divisor */
    REG(0) = d;
    REG(1) = d >> 8;
    REG(3) = 0x03;              /* 8N1 */
    REG(2) = 0x87;              /* FIFOs on, cleared, receive trigger 8 */
    ser_mcr = 0x0B;             /* DTR, RTS, OUT2 (the interrupt's gate on PC-style cards) */
    REG(4) = ser_mcr;
    ser_ier = cfg.uirq ? 0x05 : 0;      /* receive data and line status */
    (void)REG(5);
    (void)REG(0);
    (void)REG(6);
    ser_on = 1;
    REG(1) = ser_ier;
    lines_on(1);
    return 0;
}

void ser_stop(void)
{
    if (ser_on == 1) {
        REG(1) = 0;
        REG(4) = 0;             /* DTR down: the modem may hang up */
    }
    ser_on = 0;
    lines_on(0);
}

/* around a desk accessory (the application's memory is put aside, with
   the interrupt routine): no interrupts, RTS down, DTR kept */
void ser_pause(void)
{
    if (ser_on == 1) {
        REG(1) = 0;
        REG(4) = 0x01;
    }
    lines_on(0);
}

void ser_resume(void)
{
    if (ser_on == 1) {
        REG(4) = ser_mcr;
        REG(1) = ser_ier;
        (void)REG(5);
    }
    if (ser_on)
        lines_on(1);
}

/* (V91) the switcher is about to put Term aside or ask whether it may
   quit it (asideVector, from the main loop): the kernel stops calling
   SerIrq next, so the UART's interrupt (on the bus's IRQ line by
   default, a level the chip holds) and VERA's line interrupt go off
   first, as around a desk accessory; the main loop arms them again
   when Term runs again (ser_wake) */
unsigned char ser_asleep;

void ser_aside(void)
{
    ser_pause();
    ser_asleep = 1;
    LogText("TERM ASIDE");
}

void ser_wake(void)
{
    if (ser_asleep) {
        ser_asleep = 0;
        ser_resume();
        LogText("TERM BACK");
    }
}

unsigned char ser_msr(void)
{
    return ser_on == 1 ? REG(6) : 0xB0;
}

unsigned int ser_get(unsigned char *buf, unsigned int max)
{
    unsigned int head, tail = ser_rxtail, n = 0;
    unsigned char *p;
    asm("sei");
    head = ser_rxhead;
    asm("cli");
    MAPBANK(ser_rxbank);
    while (tail != head && n < max) {
        p = (unsigned char *)0xA000 + tail;
        buf[n++] = *p;
        tail = (tail + 1) & 0x1FFF;
    }
    asm("sei");
    ser_rxtail = tail;
    asm("cli");
    return n;
}

void ser_byte(unsigned char c)
{
    unsigned char h = ser_txhead + 1;
    while (h == ser_txtail)
        if (!ser_on)
            return;             /* (the interrupt empties it) */
    ser_txbuf[ser_txhead] = c;
    ser_txhead = h;
    if (ser_on == 1) {
        asm("sei");
        REG(1) = ser_ier | 0x02;        /* the transmit interrupt (or the next poll) */
        asm("cli");
    }
}

unsigned char ser_txroom(void)
{
    return ser_txtail - ser_txhead - 1;
}

/*---------------------------------------------------------------
  Telnet (RFC 854): ECHO, SGA, BINARY, TERMINAL-TYPE, NAWS
 ---------------------------------------------------------------*/
#define IAC  255
#define DONT 254
#define DO   253
#define WONT 252
#define WILL 251
#define SB   250
#define SE   240
#define O_BIN   0
#define O_ECHO  1
#define O_SGA   3
#define O_TTYPE 24
#define O_NAWS  31

unsigned char tn_on;            /* Telnet is spoken */
unsigned char tn_echo;          /* the host echoes */
unsigned char tn_bin;
unsigned char tn_seen;          /* the options negotiated, bit by option (gates) */
static unsigned char tn_state, tn_cmd, tn_sbopt, tn_sblen;
static unsigned char tn_sb[8];

static void tn_send3(unsigned char c, unsigned char o)
{
    ser_byte(IAC);
    ser_byte(c);
    ser_byte(o);
}

static void tn_sbsend(unsigned char o, const unsigned char *p, unsigned char n)
{
    ser_byte(IAC);
    ser_byte(SB);
    ser_byte(o);
    while (n--) {
        if (*p == IAC)
            ser_byte(IAC);
        ser_byte(*p++);
    }
    ser_byte(IAC);
    ser_byte(SE);
}

static void naws(void)
{
    static const unsigned char sz[4] = { 0, COLS, 0, ROWS };
    tn_sbsend(O_NAWS, sz, 4);
}

static void tn_option(unsigned char cmd, unsigned char o)
{
    switch (cmd) {
    case WILL:
        if (o == O_ECHO || o == O_SGA || o == O_BIN) {
            if (o == O_ECHO) {
                if (tn_echo)
                    return;
                tn_echo = 1;
            }
            if (o == O_BIN) {
                if (tn_bin & 1)
                    return;
                tn_bin |= 1;
            }
            tn_send3(DO, o);
        } else
            tn_send3(DONT, o);
        break;
    case WONT:
        if (o == O_ECHO && tn_echo) {
            tn_echo = 0;
            tn_send3(DONT, o);
        }
        break;
    case DO:
        if (o == O_TTYPE || o == O_NAWS || o == O_SGA || o == O_BIN) {
            if (o == O_BIN) {
                if (tn_bin & 2)
                    return;
                tn_bin |= 2;
            }
            tn_send3(WILL, o);
            if (o == O_NAWS)
                naws();
        } else
            tn_send3(WONT, o);
        break;
    case DONT:
        tn_send3(WONT, o);
        break;
    }
    if (o < 32)
        tn_seen |= o == O_ECHO ? 1 : o == O_SGA ? 2 : o == O_BIN ? 4 : o == O_TTYPE ? 8 : o == O_NAWS ? 16 : 0;
}

static void deliver(unsigned char c);

static void rx(unsigned char c)
{
    if (!tn_on) {
        deliver(c);
        return;
    }
    switch (tn_state) {
    case 0:
        if (c == IAC)
            tn_state = 1;
        else
            deliver(c);
        break;
    case 1:
        tn_state = 0;
        if (c == IAC)
            deliver(c);
        else if (c >= WILL && c <= DONT) {
            tn_cmd = c;
            tn_state = 2;
        } else if (c == SB) {
            tn_state = 3;
        }
        break;
    case 2:
        tn_state = 0;
        tn_option(tn_cmd, c);
        break;
    case 3:
        tn_sbopt = c;
        tn_sblen = 0;
        tn_state = 4;
        break;
    case 4:
        if (c == IAC)
            tn_state = 5;
        else if (tn_sblen < sizeof tn_sb)
            tn_sb[tn_sblen++] = c;
        break;
    case 5:
        if (c == SE) {
            tn_state = 0;
            if (tn_sbopt == O_TTYPE && tn_sblen && tn_sb[0] == 1) {
                static const unsigned char is[] = { 0, 'A', 'N', 'S', 'I' };
                tn_sbsend(O_TTYPE, is, 5);
                tn_seen |= 32;
            }
        } else {
            tn_state = 4;
            if (tn_sblen < sizeof tn_sb)
                tn_sb[tn_sblen++] = c;
        }
        break;
    }
}

void net_send(unsigned char c)
{
    if (tn_on && c == IAC)
        ser_byte(IAC);
    ser_byte(c);
}

void term_reply(const char *s)
{
    while (*s)
        net_send(*s++);
}

/*---------------------------------------------------------------
  the modem: result lines, dialling, hanging up
 ---------------------------------------------------------------*/
unsigned char online;
unsigned int online_since;      /* ser_frames / 60 at CONNECT */
unsigned char dialing;          /* 1 init sent, 2 dial sent */
static unsigned int dtimer;
static char target[48];
unsigned char hangup_state;
static unsigned int htimer;
static char mline[64];
static unsigned char mlen;
unsigned char got_ok;
unsigned char clockwant;        /* 1: a time line is looked for */
struct when net_time;           /* the last date and time seen */
unsigned char net_time_ok;
unsigned long rx_total;

static void modem_line(void)
{
    mline[mlen] = 0;
    if (!memcmp(mline, "CONNECT", 7)) {
        online = 1;
        online_since = ser_frames;
        dialing = 0;
        status();
    } else if (!memcmp(mline, "NO CARRIER", 10) || !memcmp(mline, "NO ANSWER", 9) || !memcmp(mline, "BUSY", 4)) {
        if (online || dialing)
            net_dropped();
        online = 0;
        dialing = 0;
        tn_on = tn_echo = tn_bin = 0;
        status();
    } else if (!strcmp(mline, "OK")) {
        got_ok = 1;
    } else if (!strcmp(mline, "ERROR")) {
        got_ok = 2;
    }
    if (parse_time(mline, &net_time))
        net_time_ok = 1;
}

static void deliver(unsigned char c)
{
    ++rx_total;
    capture_byte(c);
    if (xm_active) {
        xm_byte(c);
        return;
    }
    if (c == 13 || c == 10) {
        if (mlen)
            modem_line();
        mlen = 0;
    } else if (c >= 32 && mlen < sizeof mline - 1)
        mline[mlen++] = c;
    vt_put(c);
}

void net_rx(const unsigned char *p, unsigned int n)
{
    while (n--)
        rx(*p++);
}

static void sends(const char *s)
{
    while (*s)
        ser_byte(*s++);
}

/* dial: an AT command as it is, else "host:port" (Zimodem: ATDT) */
void net_dial(const char *s)
{
    unsigned char at = (s[0] | 0x20) == 'a' && (s[1] | 0x20) == 't';
    strncpy(target, s, sizeof target - 1);
    tn_on = tn_echo = tn_bin = 0;
    tn_seen = 0;
    tn_state = 0;
    if (!at && cfg.telnet != 2)
        tn_auto = 1;
    else
        tn_auto = cfg.telnet == 1;
    got_ok = 0;
    if (cfg.init[0]) {
        sends(cfg.init);
        ser_byte(13);
        dialing = 1;
    } else
        dialing = 2;
    dtimer = ser_frames;
    if (dialing == 2)
        net_tick();
    status();
}

unsigned char tn_auto;

void net_hangup(void)
{
    if (!online && !dialing)
        return;
    dialing = 0;
    hangup_state = 1;
    htimer = ser_frames;
    status();
}

void net_tick(void)
{
    unsigned int t = ser_frames;
    if (dialing == 1 && (got_ok || t - dtimer > 120)) {
        dialing = 2;
        dtimer = t;
    }
    if (dialing == 2 && dtimer != 0xFFFF) {
        if ((target[0] | 0x20) == 'a' && (target[1] | 0x20) == 't')
            sends(target);
        else {
            sends("ATDT");
            sends(target);
        }
        ser_byte(13);
        dtimer = 0xFFFF;
        dialing = 3;
        tn_on = tn_auto;
    }
    if (hangup_state == 1 && t - htimer > 70) {
        sends("+++");
        hangup_state = 2;
        htimer = t;
    } else if (hangup_state == 2 && t - htimer > 70) {
        sends("ATH0");
        ser_byte(13);
        hangup_state = 0;
        online = 0;
        tn_on = tn_echo = tn_bin = 0;
        status();
    }
    xm_tick();
}

/*---------------------------------------------------------------
  the date and time in a line: YY-MM-DD HH:MM:SS (the daytime service,
  NIST's format) or YYYY-MM-DD[ T]HH:MM:SS (ISO); UTC
 ---------------------------------------------------------------*/
static unsigned char num(const char *s, unsigned char n, unsigned int *v)
{
    unsigned int x = 0;
    while (n--) {
        if (*s < '0' || *s > '9')
            return 0;
        x = x * 10 + (*s++ - '0');
    }
    *v = x;
    return 1;
}

unsigned char parse_time(const char *s, struct when *w)
{
    unsigned int y, mo, d, h, mi, se;
    unsigned char yl;
    for (; *s; ++s) {
        for (yl = 4; yl >= 2; yl -= 2) {
            const char *p = s;
            if (!num(p, yl, &y) || p[yl] != '-')
                continue;
            p += yl + 1;
            if (!num(p, 2, &mo) || p[2] != '-' || !num(p + 3, 2, &d) || (p[5] != ' ' && p[5] != 'T'))
                continue;
            p += 6;
            if (!num(p, 2, &h) || p[2] != ':' || !num(p + 3, 2, &mi) || p[5] != ':' || !num(p + 6, 2, &se))
                continue;
            if (yl == 2)
                y += 2000;
            if (y < 2000 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 60)
                continue;
            w->yr = y - 1900;
            w->mon = mo;
            w->dy = d;
            w->hr = h;
            w->mn = mi;
            w->sec = se > 59 ? 59 : se;
            return 1;
        }
    }
    return 0;
}

static unsigned char mdays(unsigned char m, unsigned char y)
{
    static const unsigned char dm[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return m == 2 && !(y & 3) ? 29 : dm[m - 1];
}

/* UTC + quarter hours, across days, months and years */
void time_zone(struct when *w, signed char q)
{
    int m = (int)w->hr * 60 + w->mn + (int)q * 15;
    while (m < 0) {
        m += 1440;
        if (--w->dy == 0) {
            if (--w->mon == 0) {
                w->mon = 12;
                --w->yr;
            }
            w->dy = mdays(w->mon, w->yr);
        }
    }
    while (m >= 1440) {
        m -= 1440;
        if (++w->dy > mdays(w->mon, w->yr)) {
            w->dy = 1;
            if (++w->mon > 12) {
                w->mon = 1;
                ++w->yr;
            }
        }
    }
    w->hr = m / 60;
    w->mn = m % 60;
}

/* 1 Monday .. 7 Sunday (the RTC's day of the week; Sakamoto's method) */
unsigned char weekday(const struct when *w)
{
    static const unsigned char t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    unsigned int y = 1900 + w->yr - (w->mon < 3);
    unsigned char d = (y + y / 4 - y / 100 + y / 400 + t[w->mon - 1] + w->dy) % 7;
    return d ? d : 7;
}
