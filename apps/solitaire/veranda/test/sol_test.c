/*
 * Veranda Solitaire's test build (make test): the scripted session
 * (play.s plays host/mkscript's script.bin, record 7, through
 * mouseSource), the state written on
 * Alt + W to STATE1, STATE2 ... in tools/../src/dump.c's lines, so the
 * gate can compare it with the host engine's; the emulator's cycle
 * counter for the timings. GPL-3.0-or-later (see COPYING).
 */
#include <string.h>
#include "../ui.h"
#include "../../src/dump.h"

extern void __fastcall__ sol_play(u8 bank);

/* the script: host/mkscript's script.bin, record 7 */
void sol_test_start(void)
{
#ifndef SOL_REALMOUSE       /* (else the X16's mouse: x16emu -mousescript) */
    u8 b = ovl_bank(7);
    if (b)
        sol_play(b);
    else
        LogText("SOL NO SCRIPT");
#endif
}

static char buf[900];
static char name[8] = "STATE0";
static char sname[9] = "SCREEN0";

/* the state, and the screen (VERA's foreground, 38,400 bytes) */
void sol_test_key(void)
{
    u8 h, k, was = busy;
    u16 n = sol_dump(buf, 1), i;
    busy = 1;                   /* the script waits */
    ++name[5];
    if (name[5] == '?' || name[5] == '*')
        ++name[5];              /* (wildcards in a file name: not names) */
    h = FOpen(name, 'w');
    if (h >= 2) {
        FWrite(h, buf, n);
        FClose(h);
    }
    sname[6] = name[5];
    h = FOpen(sname, 'w');
    if (h >= 2) {
        for (k = 0; k < 48; ++k) {
            *(volatile u8 *)0x9F25 = 0;             /* VERA: port 0 */
            i = (u16)k * 800;
            *(volatile u8 *)0x9F20 = (u8)i;
            *(volatile u8 *)0x9F21 = (u8)(i >> 8);
            *(volatile u8 *)0x9F22 = 0x10;          /* increment 1 */
            for (i = 0; i < 800; ++i)
                buf[i] = *(volatile u8 *)0x9F23;
            FWrite(h, buf, 800);
        }
        FClose(h);
    }
    busy = was;
    LogText(name);
}

/* SOL_SLOWTICK: the second's process holds the main loop for 12 frames
   (sol_slow 1 meanwhile, for the mouse script's "wait"), as a slow pass of
   the main loop: a release and a press in that time reach the program as
   one press while it drags (gate N) */
volatile u8 sol_slow;

void sol_test_slow(void)
{
    u8 f0 = frames;
    sol_slow = 1;
    while ((u8)(frames - f0) < 12)
        ;
    sol_slow = 0;
}

/* the emulator's cycle counter ($9FB8: a write resets it; reading $9FB8
   latches it, then $9FB9-$9FBB): "TIME <what> <cycles, hex>" */
static const char hex[] = "0123456789ABCDEF";

void clock_reset(void)
{
    *(volatile unsigned char *)0x9FB8 = 0;
}

void clock_log(const char *what)
{
    static char line[32];
    unsigned char c[4], i, n = 5;
    c[0] = *(volatile unsigned char *)0x9FB8;
    c[1] = *(volatile unsigned char *)0x9FB9;
    c[2] = *(volatile unsigned char *)0x9FBA;
    c[3] = *(volatile unsigned char *)0x9FBB;
    memcpy(line, "TIME ", 5);
    while (*what)
        line[n++] = *what++;
    line[n++] = ' ';
    for (i = 4; i-- > 0;) {
        line[n++] = hex[c[i] >> 4];
        line[n++] = hex[c[i] & 15];
    }
    line[n] = 0;
    LogText(line);
}
