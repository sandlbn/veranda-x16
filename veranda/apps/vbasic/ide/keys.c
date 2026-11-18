/* VeraBASIC, the editor (DECISIONS V93): B_VIEW, the bank the kernel calls
   while the editor shows (appBank): the keys, the pointer, the main loop's
   pass, the bars; textkit's view.c is in this bank too (it draws; the
   rows' colours are B_LEX's). The menus' actions are B_CMD's (LOW's mn_*
   stubs). Veranda Edit's main.c is the model.

   MIT licence (LICENSE). */
#include <string.h>
#include <theme.h>
#include <vrwin.h>
#include "ide.h"

static unsigned char dragging;

/* (other banks: the view drawn, the cursor off) */
void view_off(void)
{
    view_cursor(0);
}

/*---------------------------------------------------------------
  the bars (textkit's view.c calls them)
 ---------------------------------------------------------------*/
static const char *const states[] = { "Ready", "Modified" };

/* the status bar: the cursor's line:column, a message or the syntax of
   the keyword at the cursor, the state at the right */
void tk_status(void)
{
    char *p = tk_line, *e, *s;
    unsigned char n, k;
    memset(tk_line, ' ', 80);
    p = tk_put_s(p + 1, "");
    p = tk_put_ul(p, cur_line + 1);
    *p++ = ':';
    p = tk_put_ul(p, (unsigned long)view_col() + 1);
    e = (char *)(stop_line ? "Stopped" : err_line ? "Error" : states[modified]);
    k = strlen(e);
    memcpy(tk_line + 79 - k, e, k);
    p = tk_line + (p < tk_line + 9 ? 10 : (p - tk_line) + 2);
    s = status_msg[0] ? status_msg : syn_line;
    n = strlen(s);
    if (n > tk_line + 78 - k - p)
        n = tk_line + 78 - k - p;
    memcpy(p, s, n);
    tk_line[80] = 0;
    PutCards(tk_line, vb_font, 0, 80, STATY + 5, 0);
}

/* the title band: the program, modified, its folder */
void tk_title(void)
{
    unsigned char l;
    char *q = tk_line;
    q = tk_put_s(q, "VeraBASIC: ");
    q = tk_put_s(q, docname[0] ? docname : "Untitled");
    if (modified)
        q = tk_put_s(q, " *");
    q = tk_put_s(q, "  -  ");
    l = strlen((char *)curDirName);
    if (l > 30) {
        q = tk_put_s(q, "...");
        q = tk_put_s(q, (char *)curDirName + l - 27);
    } else
        q = tk_put_s(q, (char *)curDirName);
    *q = 0;
    vr_win_title(&tk_win, tk_line);
}


static void type(unsigned char c)
{
    if (HAS_SEL()) {
        undo_run(0);
        ed_delete_sel();
        undo_join(U_INS);
    } else
        undo_run(U_INS);
    ed_insert(&c, 1);
}

static void letter(unsigned char c, unsigned char sh)
{
    switch (c | 0x20) {
    case 'a': mn_selall(); break;
    case 'c': mn_copy(); break;
    case 'x': mn_cut(); break;
    case 'v': mn_paste(); break;
    case 'z': mn_undo(); break;
    case 'f': if (sh) mn_format(); else mn_find(); break;
    case 'h': mn_replnext(); break;
    case 'l': case 'g': mn_goto(); break;
    case 'n': mn_new(); break;
    case 'o': mn_open(); break;
    case 's': if (sh) mn_saveas(); else mn_save(); break;
    case 'q': mn_quit(); break;
    case 'r': mn_run(); break;
    case 'k': mn_check(); break;
    case 'b': move_to(gb_len, sh); break;      /* Ctrl+PgDn */
    }
}

void ide_key(void)
{
    unsigned char k = keyData, m = keyDataMods, sh = m & 1, ctrl = m & 2;
    if (help_open) {                    /* the help viewer's keys */
        XCALL(B_HELP, help_key);
        return;
    }
    need_fix = 1;
    if (status_msg[0]) {                /* a message lasts until the next key */
        status_msg[0] = 0;
        bars_dirty |= B_STATUS;
    }
    if (m & KEYM_ARROW) {
        undo_run(0);
        switch (k) {
        case KEY_LEFT:
            if (ctrl)
                move_word(-1, sh);
            else if (HAS_SEL() && !sh)
                move_to(sel_lo(), 0);
            else
                move_to(cur ? cur - 1 : 0, sh);
            goal = 0xFFFF;
            break;
        case KEY_RIGHT:
            if (ctrl)
                move_word(1, sh);
            else if (HAS_SEL() && !sh)
                move_to(sel_hi(), 0);
            else
                move_to(cur < gb_len ? cur + 1 : cur, sh);
            goal = 0xFFFF;
            break;
        case KEY_UP:
            move_line(-1, sh);
            break;
        case KEY_DOWN:
            move_line(1, sh);
            break;
        }
        return;
    }
    if (ctrl && k >= 1 && k <= 26) {
        undo_run(0);
        letter(k + 0x60, sh);
        return;
    }
    if (k >= 0x80 && (m & 4)) {                 /* Alt + a letter (shortcuts) */
        undo_run(0);
        letter(k & 0x7F, sh);
        return;
    }
    switch (k) {
    case KEY_HOME:
        undo_run(0);
        move_home(sh);
        goal = 0;
        return;
    case KEY_CLEAR:
        undo_run(0);
        move_home(1);
        return;
    case KEY_END:
        undo_run(0);
        if (ctrl)
            move_to(gb_len, sh);
        else
            move_end(sh);
        return;
    case KEY_PGUP:
        undo_run(0);
        if (ctrl)
            move_to(0, sh);
        else
            move_page(-1, sh);
        return;
    case KEY_PGDN:
        undo_run(0);
        move_page(1, sh);
        return;
    case 8:
        undo_run(HAS_SEL() ? 0 : 2);
        ed_backspace();
        return;
    case KEY_DELETE:
        undo_run(HAS_SEL() ? 0 : 3);
        ed_delete();
        return;
    case CR:
    case 0x8D:
        mn_newline();
        return;
    case KEY_TAB:                       /* two blanks: VeraBASIC's indentation */
        type(' ');
        type(' ');
        return;
    case KEY_ESC:
        undo_run(0);
        sel_clear();
        return;
    case KEY_F1:
        undo_run(0);
        mn_help();
        return;
    case KEY_F3:
        undo_run(0);
        mn_findnext();
        return;
    case KEY_F5:
        undo_run(0);
        mn_output();
        return;
    case KEY_F8:
        undo_run(0);
        mn_nexterr();
        return;
    case 0xA0:
        if (sh)
            k = ' ';
        break;
    }
    if ((k >= 32 && k < 127) || k >= 160)
        type(k);
}

/*---------------------------------------------------------------
  the pointer
 ---------------------------------------------------------------*/
static void mouse_xy(unsigned int *x, unsigned int *y)
{
    do {
        *x = mouseXPos;
        *y = mouseYPosW;
    } while (*x != mouseXPos || *y != mouseYPosW);
}

void ide_press(void)
{
    unsigned int x, y;
    unsigned long p;
    unsigned char sh;
    if (help_open) {
        keyData = 0;                    /* (a press: the viewer takes keyData 0) */
        XCALL(B_HELP, help_key);
        return;
    }
    if (mouseData & 0x80) {
        dragging = 0;
        return;
    }
    mouse_xy(&x, &y);
    need_fix = 1;
    undo_run(0);
    if (view_hit(x, y, &p)) {
        sh = GetKeyMods() & 1;
        if (!sh && p == cur && !HAS_SEL() && dblClickCount) {
            dblClickCount = 0;
            select_word();
            return;
        }
        move_to(p, sh);
        goal = 0xFFFF;
        dragging = 1;
        dblClickCount = 30;
    } else if (x >= SBX * 8 && y >= TY && y < TY + ROWS * 8) {
        view_scroll(y < TY + 8 * (ROWS / 2) ? -(ROWS - 1) : ROWS - 1);
    }
}

static void drag(void)
{
    unsigned int x, y;
    unsigned long p;
    if (mouseData & 0x80) {
        dragging = 0;
        return;
    }
    mouse_xy(&x, &y);
    if (y < TY) {
        move_line(-1, 1);
        need_fix = 1;
        return;
    }
    if (y >= TY + ROWS * 8) {
        move_line(1, 1);
        need_fix = 1;
        return;
    }
    if (x >= (X0 + COLS) * 8)
        x = (X0 + COLS) * 8 - 1;
    if (view_hit(x, y, &p) && p != cur) {
        move_to(p, 1);
        goal = 0xFFFF;
        need_fix = 1;
    }
}

/*---------------------------------------------------------------
  the main loop's pass (LOW's appMain)
 ---------------------------------------------------------------*/
static unsigned long last_ls = NOWHERE;
static unsigned int last_col = 0xFFFF;

static void state_log(void)
{
#ifdef IDE_TEST
    ide_logn("IDE STATE ", test_log);
    ide_logn(" ", cur_line + 1);
    ide_logn(" ", view_col() + 1);
    ide_logn(" ", gb_len);
    ide_logn(" ", nlines);
    ide_logn(" ", modified);
    ide_logn(" ", err_line);
    ide_logn(" ", stop_line);
    ide_log(" ");
    ide_log(docname);
    ide_log("\n");
    test_log = 0;
#endif
}

void ide_tick(void)
{
    signed char w;
    unsigned int y;
    edit_idle = 0;
    if (help_open) {
        edit_idle = 1;
        return;
    }
    if (dragging)
        drag();
    asm("sei");
    w = (signed char)mouseWheel;
    mouseWheel = 0;
    asm("cli");
    if (w && menuNumber == 0)
        view_scroll(3 * w);
    y = mouseYPosW;
    SetPointer(y >= TY && y < TY + ROWS * 8 && mouseXPos >= X0 * 8 && mouseXPos < (X0 + COLS) * 8
               ? PTR_IBEAM : PTR_ARROW);
    if (pressFlag & SET_KEYPRESS)
        return;                         /* draw once the keys waiting are in */
    if (need_fix) {
        need_fix = 0;
        if (cur_ls != last_ls || view_col() != last_col) {
            last_ls = cur_ls;           /* the syntax of the word at the cursor */
            XCALL(B_SYN, help_syntax);
            bars_dirty |= B_STATUS;
        }
        if (goal == 0xFFFF) {
            view_fix();
            goal = view_col();
        } else
            view_fix();
        last_col = view_col();
        return;
    }
    if (menuNumber == 0)
        view_blink();
    edit_idle = 1;
    if (test_log)
        state_log();
}

