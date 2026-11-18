/* Veranda Edit: the 8 x 8 font, the X16 ROM's ISO-8859-15 set (ROM bank 6,
   the charsets), made ISO-8859-1: the eight glyphs where the two differ
   (the euro, the caron letters, the OE ligatures, Y diaeresis) are Latin-1's
   (currency, broken bar, diaeresis, acute, cedilla, the three fractions).
   Glyphs 0-31 are the ROM's inverted @ A-Z [ \ ] ^ _, so control
   characters show as such; glyph $80 shows a NUL byte (PutCards stops at a
   0), as glyph 0.

   MIT licence (see LICENSE). */
#include <string.h>
#include "edit.h"

unsigned char font[2048];

/* glyph 0 (an inverted @) and 'A' of the ROM's ISO set: how it is found */
static const unsigned char sig0[8] = { 0xC3, 0x99, 0x91, 0x91, 0x91, 0x9F, 0xC1, 0xFF };
static const unsigned char sigA[8] = { 0x18, 0x3C, 0x24, 0x66, 0x7E, 0x66, 0x66, 0x00 };

static const unsigned char latin1[8][9] = {
    { 0xA4, 0x00, 0x42, 0x3C, 0x24, 0x24, 0x3C, 0x42, 0x00 },  /* currency */
    { 0xA6, 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00 },  /* broken bar */
    { 0xA8, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  /* diaeresis */
    { 0xB4, 0x0C, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00 },  /* acute */
    { 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x0C, 0x38 },  /* cedilla */
    { 0xBC, 0x42, 0xC4, 0x48, 0x54, 0x2C, 0x54, 0x8F, 0x04 },  /* 1/4 */
    { 0xBD, 0x42, 0xC4, 0x48, 0x50, 0x2E, 0x41, 0x86, 0x0F },  /* 1/2 */
    { 0xBE, 0xE2, 0x24, 0x68, 0x34, 0xEC, 0x54, 0x8F, 0x04 },  /* 3/4 */
};

void font_load(void)
{
    unsigned char keep, i, ok = 0;
    unsigned int a;
    asm("sei");                 /* ROM bank 6 has no interrupt vectors */
    keep = *(unsigned char *)1;
    *(unsigned char *)1 = 6;
    for (a = 0xC000; a; a += 0x800)
        if (!memcmp((void *)a, sig0, 8) && !memcmp((void *)(a + 0x41 * 8), sigA, 8)) {
            memcpy(font, (void *)a, 2048);
            ok = 1;
            break;
        }
    if (!ok)
        memcpy(font, (void *)0xC800, 2048);
    *(unsigned char *)1 = keep;
    asm("cli");
    for (i = 0; i < 8; ++i)
        memcpy(font + latin1[i][0] * 8, latin1[i] + 1, 8);
    memcpy(font + 0x80 * 8, font, 8);
}
