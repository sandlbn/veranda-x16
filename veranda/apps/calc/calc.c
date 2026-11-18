/* Calculator: a Veranda desk accessory in C.

   A window in the middle of the screen, over the application that opened
   it (GetFile on a DESK_ACC file: docs/APPFORMAT.md). A display and 16
   keys: digits, + - * /, = and C; the close box (top left) or Escape
   ends it with RstrAppl, which gives the application back exactly as it
   was. Keys work too: 0-9 + - * / = Return C, Escape.

   Whole numbers (32 bits), as a pocket calculator: an operator applies
   the one before it.

   MIT licence (see LICENSE). */
#include <veranda.h>
#include <vr.h>
#include <stdlib.h>
#include <string.h>

#define W       200             /* the window */
#define H       232
#define BW      44              /* a key */
#define BH      36
#define GAP     4
#define KX      8               /* the keys' left, top in the window */
#define KY      60

static unsigned int x0, y0;     /* the window's top left */
static long acc, cur;
static char op;                 /* the pending operator, 0 none */
static unsigned char fresh;     /* the next digit starts a new number */
static char buf[16];
static char line[24];

static const char keys[16] = {
    '7', '8', '9', '/',
    '4', '5', '6', '*',
    '1', '2', '3', '-',
    'C', '0', '=', '+' };

#ifdef CALC_TEST
void calc_test_start(void);
void calc_test_clock_reset(void);
void calc_test_clock_log(const char *what);
#endif

static unsigned int strw(const char *s)
{
    unsigned int w = 0;
    while (*s)
        w += GetCharWidth(*s++);
    return w;
}

static void show(void)
{
    unsigned int w;
    ltoa(cur, buf, 10);
    SetPattern(0);
    WRectangle(x0 + 10, y0 + 26, x0 + W - 11, y0 + 48);
    w = strw(buf);
    WPutString(buf, x0 + W - 14 - w, y0 + 42);
}

static void draw_key(unsigned char i)
{
    unsigned int x = x0 + KX + (i & 3) * (BW + GAP), y = y0 + KY + (i >> 2) * (BH + GAP);
    char s[2];
    s[0] = keys[i];
    s[1] = 0;
    SetPattern(0);
    WRectangle(x, y, x + BW - 1, y + BH - 1);
    WFrameRectangle(0xFF, x, y, x + BW - 1, y + BH - 1);
    WFrameRectangle(0xFF, x + 1, y + 1, x + BW - 2, y + BH - 2);
    WPutString(s, x + (BW - GetCharWidth(s[0])) / 2, y + BH / 2 + 3);
}

static void draw(void)
{
    unsigned char i;
    dispBufferOn = ST_WR_FORE;          /* the application's screen is put aside whole */
    SetPattern(0);
    WRectangle(x0, y0, x0 + W - 1, y0 + H - 1);
    WFrameRectangle(0xFF, x0, y0, x0 + W - 1, y0 + H - 1);
    SetPattern(6);                      /* the title bar */
    WRectangle(x0 + 1, y0 + 1, x0 + W - 2, y0 + 15);
    SetPattern(0);
    WRectangle(x0 + 6, y0 + 3, x0 + 17, y0 + 13);       /* the close box */
    WFrameRectangle(0xFF, x0 + 6, y0 + 3, x0 + 17, y0 + 13);
    WRectangle(x0 + 70, y0 + 3, x0 + 70 + strw("Calculator") + 5, y0 + 13);
    WPutString("Calculator", x0 + 73, y0 + 11);
    WHorizontalLine(0xFF, x0, x0 + W - 1, y0 + 16);
    WFrameRectangle(0xFF, x0 + 8, y0 + 24, x0 + W - 9, y0 + 50);
    for (i = 0; i < 16; ++i)
        draw_key(i);
    show();
}

static void calc_close(void)
{
    LogText("CALC CLOSED");
#ifdef CALC_TEST
    calc_test_clock_reset();
#endif
    RstrAppl();
}

static long apply(long a, char o, long b)
{
    switch (o) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': return b ? a / b : 0;
    }
    return b;
}

static void key(char c)
{
    if (c >= '0' && c <= '9') {
        if (fresh) {
            cur = 0;
            fresh = 0;
        }
        if (cur < 100000000L && cur > -100000000L)
            cur = cur * 10 + (cur < 0 ? -(c - '0') : c - '0');
    } else if (c == 'C' || c == 'c') {
        acc = cur = 0;
        op = 0;
        fresh = 0;
    } else if (c == '+' || c == '-' || c == '*' || c == '/' || c == '=' || c == 13) {
        acc = op ? apply(acc, op, cur) : cur;
        cur = acc;
        op = (c == '=' || c == 13) ? 0 : c;
        fresh = 1;
        if (!op) {
            strcpy(line, "CALC = ");
            ltoa(cur, line + 7, 10);
            LogText(line);
        }
    } else if (c == 27 || c == 'q')
        calc_close();
    else
        return;
    show();
}

/* otherPressVec: a press on a key or the close box */
static void press(void)
{
    unsigned int x = mouseXPos, y = mouseYPosW, kx, ky;
    unsigned char col, row;
    if (mouseData & 0x80)
        return;
    if (x >= x0 + 6 && x <= x0 + 17 && y >= y0 + 3 && y <= y0 + 13) {
        calc_close();
        return;
    }
    if (x < x0 + KX || y < y0 + KY)
        return;
    kx = x - x0 - KX;
    ky = y - y0 - KY;
    col = kx / (BW + GAP);
    row = ky / (BH + GAP);
    if (col > 3 || row > 3 || kx % (BW + GAP) >= BW || ky % (BH + GAP) >= BH)
        return;
    key(keys[row * 4 + col]);
}

static void typed(void)
{
    key(keyData);
}

/* appMain: the I-beam over the display (it takes typed digits), the
   arrow elsewhere (DECISIONS V51) */
static void track(void)
{
    unsigned int x, y;
    do {
        x = mouseXPos;
        y = mouseYPosW;
    } while (x != mouseXPos || y != mouseYPosW);
    SetPointer(x >= x0 + 10 && x <= x0 + W - 11 && y >= y0 + 26 && y <= y0 + 48 ? PTR_IBEAM : PTR_ARROW);
}

int main(void)
{
#ifdef CALC_TEST
    calc_test_clock_log("open");        /* since Paint's GetFile */
#endif
    x0 = (screenWidth - W) / 2;
    y0 = (screenHeight - H) / 2;
    draw();
    otherPressVec = VR_FN(press);
    keyVector = VR_FN(typed);
    appMain = VR_FN(track);
    LogText("CALC RUNNING");
#ifdef CALC_TEST
    calc_test_start();
#endif
    return 0;
}
