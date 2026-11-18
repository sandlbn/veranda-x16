/* VeraBASIC, the editor (DECISIONS V93): B_FILE, the commands on the
   document (textkit's file.c and scrap.c are in this bank too), the menus'
   layout (Veranda Edit's menus.c), and the text given to the runtime's
   side: a run of banks the tokenizer reads (vbprep.s compile, _vt_inmem).

   MIT licence (LICENSE). */
#include <string.h>
#include "ide.h"

unsigned char export_ok;

void cmd_new(void) { m_new(); err_line = stop_line = 0; XCALL(B_VIEW, view_all); }
void cmd_open(void) { m_open(); err_line = stop_line = 0; XCALL(B_VIEW, view_all); }
void cmd_save(void) { m_save(); }
void cmd_saveas(void) { m_saveas(); }
void cmd_quit(void) { m_quit(); }
void cmd_cut(void) { m_cut(); }
void cmd_copy(void) { m_copy(); }
void cmd_paste(void) { m_paste(); }

/* the text (LF line ends) into a run of banks: vt_src, vt_srclen; the
   program's name for the run's title */
void cmd_export(void)
{
    unsigned long len = gb_len, at;
    unsigned int k;
    unsigned char n, b;
    export_ok = 0;
    if (len >= 0xFF00) {
        message("The program is too big for VeraBASIC", "(64 KB of text at most).");
        return;
    }
    n = (unsigned char)((len + 8191) >> 13);
    if (!n)
        n = 1;
    b = BankAlloc(n);
    if (!b) {
        message("There is not enough memory", "to check the program (RAM banks).");
        return;
    }
    for (at = 0; at < len; at += k) {   /* (TK_IOBUF divides a bank) */
        k = len - at > TK_IOBUF ? TK_IOBUF : (unsigned int)(len - at);
        gb_a = at;
        gb_n = k;
        gb_lp = iobuf;
        gb_read();
        BankCopy(iobuf, (void *)(0xA000 + ((unsigned int)at & 0x1FFF)), k, 0, b + (unsigned char)(at >> 13));
    }
    vt_src = b;
    vt_srclen = (unsigned int)len;
    strcpy(vb_docname, docname[0] ? docname : TK_UNTITLED);
    export_ok = 1;
}

/*---------------------------------------------------------------
  the menus' texts: a name, its shortcut aligned after the longest
 ---------------------------------------------------------------*/
struct item { const char *text; unsigned char type; void *p; };
struct menuh { unsigned char top, bottom; unsigned int left, right; unsigned char flags; struct item i[1]; };
extern struct menuh mi_vb, mi_file, mi_edit, mi_search, mi_run, mi_help, mi_bar;
extern char mi_text[900];

static const char *const names[34][2] = {
    { "New", "Ctrl+N" }, { "Open...", "Ctrl+O" }, { "Save", "Ctrl+S" }, { "Save as...", "Ctrl+Shift+S" },
    { "Quit", "Ctrl+Q" },
    { "Undo", "Ctrl+Z" }, { "Cut", "Ctrl+X" }, { "Copy", "Ctrl+C" }, { "Paste", "Ctrl+V" },
    { "Select all", "Ctrl+A" }, { "Format", "Ctrl+Shift+F" }, { "Line numbers", "" },
    { "Find and replace...", "Ctrl+F" }, { "Find next", "F3" }, { "Replace, find next", "Ctrl+H" },
    { "Go to line...", "Ctrl+L" }, { "Next error", "F8" },
    { "Run", "Ctrl+R" }, { "Check", "Ctrl+K" }, { "Stop", "Ctrl+C" }, { "Output", "F5" },
    { "Continue", "F6" }, { "Step", "F7" }, { "Step over", "F10" }, { "Step out", "F11" },
    { "Breakpoint", "F9" }, { "Add a watch...", "Ctrl+W" }, { "Trace (TRON)", "" }, { "The trace", "F12" },
    { "Help on the word", "F1" }, { "Contents", "" }, { "Index", "" }, { "Tutorial", "" },
    { "Error messages", "" },
};
static struct menuh *const menus[5] = { &mi_file, &mi_edit, &mi_search, &mi_run, &mi_help };
static const unsigned char first[6] = { 0, 5, 12, 17, 29, 34 };

static unsigned int mi_at;

static unsigned int strw(const char *s)
{
    unsigned int w = 0;
    while (*s)
        w += GetCharWidth(*s++);
    return w;
}

static unsigned int build(unsigned char m)
{
    unsigned char i, a = first[m], b = first[m + 1];
    unsigned int w = 0, mx = 0, sw = GetCharWidth(' ');
    char *p;
    for (i = a; i < b; ++i)
        if (strw(names[i][0]) > w)
            w = strw(names[i][0]);
    w += 16;
    for (i = a; i < b; ++i) {
        p = mi_text + mi_at;
        strcpy(p, names[i][0]);
        if (names[i][1][0]) {
            while (strw(p) + sw <= w)
                strcat(p, " ");
            strcat(p, names[i][1]);
        }
        menus[m]->i[i - a].text = p;
        mi_at += strlen(p) + 1;
        if (strw(p) > mx)
            mx = strw(p);
    }
    return mx;
}

void cmd_layout(void)
{
    static struct menuh *const subs[6] = { &mi_vb, &mi_file, &mi_edit, &mi_search, &mi_run, &mi_help };
    unsigned int x = 0, w;
    unsigned char i;
    UseFont(FONT_UI);                   /* (the menus' font: theme_menus, V79) */
    mi_at = 0;
    for (i = 0; i < 6; ++i) {
        w = (i ? build(i - 1) : strw("About VeraBASIC")) + 8;
        subs[i]->left = x;
        subs[i]->right = x + w;
        barx[i] = x + 4;
        x += strw(mi_bar.i[i].text) + 8;
    }
    mi_bar.right = x + 64;              /* (past the kernel's bar end, V74) */
    UseSystemFont();
}
