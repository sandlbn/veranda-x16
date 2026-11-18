/* Veranda Edit: the menus (their tables in main RAM; layout() in
   overlay 2, misc.c): the items' widths from the system font, each
   shortcut after the longest name of its menu.

   MIT licence (see LICENSE). */
#include <string.h>
#include "edit.h"
#pragma code-name ("OVERLAY2")

struct item { const char *text; unsigned char type; void *p; };
struct menu2 { unsigned char top, bottom; unsigned int left, right; unsigned char flags; struct item i[2]; };
struct menu4 { unsigned char top, bottom; unsigned int left, right; unsigned char flags; struct item i[4]; };
struct menu5 { unsigned char top, bottom; unsigned int left, right; unsigned char flags; struct item i[5]; };

#define IH 14
static struct menu2 mgeos = { 14, 14 + 2 * IH, 0, 0, VERTICAL | 2, {
    { "About Veranda Edit", MENU_ACTION, m_about }, { "Desk accessory...", MENU_ACTION, m_desk } } };
static struct menu5 mfile = { 14, 14 + 5 * IH, 0, 0, VERTICAL | 5, {
    { 0, MENU_ACTION, c_new }, { 0, MENU_ACTION, c_open }, { 0, MENU_ACTION, c_save },
    { 0, MENU_ACTION, c_saveas }, { 0, MENU_ACTION, quit } } };
static struct menu5 medit = { 14, 14 + 5 * IH, 0, 0, VERTICAL | 5, {
    { 0, MENU_ACTION, m_undo }, { 0, MENU_ACTION, mc_cut }, { 0, MENU_ACTION, mc_copy },
    { 0, MENU_ACTION, mc_paste }, { 0, MENU_ACTION, m_selall } } };
static struct menu4 msearch = { 14, 14 + 4 * IH, 0, 0, VERTICAL | 4, {
    { 0, MENU_ACTION, c_find }, { 0, MENU_ACTION, c_findnext }, { 0, MENU_ACTION, c_replnext },
    { 0, MENU_ACTION, c_goto } } };
static struct menu4 mbar = { 0, 14, 0, 0, HORIZONTAL | UN_CONSTRAINED | 4, {
    { "Veranda Edit", SUB_MENU, &mgeos }, { "File", SUB_MENU, &mfile }, { "Edit", SUB_MENU, &medit },
    { "Search", SUB_MENU, &msearch } } };

/* the items: a name, its shortcut aligned (spaces) after the longest */
static const char *const names[14][2] = {
    { "New", "Ctrl+N" }, { "Open...", "Ctrl+O" }, { "Save", "Ctrl+S" }, { "Save as...", "Ctrl+Shift+S" },
    { "Quit", "Ctrl+Q" },
    { "Undo", "Ctrl+Z" }, { "Cut", "Ctrl+X" }, { "Copy", "Ctrl+C" }, { "Paste", "Ctrl+V" },
    { "Select all", "Ctrl+A" },
    { "Find and replace...", "Ctrl+F" }, { "Find next", "F3" }, { "Replace, find next", "Ctrl+R" },
    { "Go to line...", "Ctrl+L" },
};
static char itext[14][36];
static struct item *const items[14] = {
    &mfile.i[0], &mfile.i[1], &mfile.i[2], &mfile.i[3], &mfile.i[4],
    &medit.i[0], &medit.i[1], &medit.i[2], &medit.i[3], &medit.i[4],
    &msearch.i[0], &msearch.i[1], &msearch.i[2], &msearch.i[3],
};

static unsigned int strw(const char *s)
{
    unsigned int w = 0;
    while (*s)
        w += GetCharWidth(*s++);
    return w;
}

unsigned int barx[4];            /* the bar's items (the test's player) */

static unsigned int build(unsigned char a, unsigned char b)
{
    unsigned char i;
    unsigned int w = 0, m = 0, sw = GetCharWidth(' ');
    char *p;
    for (i = a; i < b; ++i)
        if (strw(names[i][0]) > w)
            w = strw(names[i][0]);
    w += 16;
    for (i = a; i < b; ++i) {
        p = itext[i];
        strcpy(p, names[i][0]);
        if (names[i][1][0]) {
            while (strw(p) + sw <= w)
                strcat(p, " ");
            strcat(p, names[i][1]);
        }
        items[i]->text = p;
        if (strw(p) > m)
            m = strw(p);
    }
    return m;
}

void layout(void)
{
    static void *const subs[4] = { &mgeos, &mfile, &medit, &msearch };
    unsigned int widths[4];
    unsigned int x = 0, w;
    unsigned char i;
    UseFont(FONT_UI);                   /* (the menus' font: theme_menus, V79) */
    widths[0] = strw("About Veranda Edit");
    widths[1] = build(0, 5);
    widths[2] = build(5, 10);
    widths[3] = build(10, 14);
    for (i = 0; i < 4; ++i) {
        w = widths[i] + 8;
        ((struct menu2 *)subs[i])->left = x;
        ((struct menu2 *)subs[i])->right = x + w;
        barx[i] = x + 4;
        x += strw(mbar.i[i].text) + 8;
    }
    mbar.right = x + 64;                /* (past the kernel's bar end, V74) */
    UseSystemFont();
}


void *const menubar = &mbar;
