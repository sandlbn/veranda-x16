/* Harbour Walls for Veranda: the menus (Veranda Look 2: the kernel draws
   the bar), the boxes, the high scores (in Veranda's system data folder,
   /VERANDA/.SYSTEM: vrpath.h, DECISIONS V71), the sound's option. */
#include <veranda.h>
#include <vr.h>
#include <vrpath.h>
#include <string.h>
#include "../src/engine.h"
#include "hw.h"

/*---------------------------------------------------------------- boxes */

/* a dialog box at its own place, y in words (WIDE_DB) */
struct boxhead { u8 pos; u16 top, bottom, left, right; };
struct txt { u8 cmd, x, y; const char *s; };
struct btn { u8 cmd, x, y; };

static void box(const void *b)
{
    sprites_hide();
    DoDlgBox((void *)b);
    resync();
}

#define HEAD(t, b, l, r) { SET_DB_POS | WIDE_DB | 1, t, b, l, r }

static const struct {
    struct boxhead h;
    struct txt t[5];
    struct btn ok;
    u8 end;
} about_box = {
    HEAD(160, 290, 160, 479),
    { { DBTXTSTR, 16, 20, "Harbour Walls 1.0" },
      { DBTXTSTR, 16, 40, "Build sea walls to calm the harbour." },
      { DBTXTSTR, 16, 56, "An original game for Veranda" },
      { DBTXTSTR, 16, 72, "and the Commander X16." },
      { DBTXTSTR, 16, 92, "(c) 2026 Marcin Spoczynski, MIT licence" } },
    { OK, 15, 106 },
    0
};

static const struct {
    struct boxhead h;
    struct txt t[9];
    struct btn ok;
    u8 end;
} help_box = {
    HEAD(120, 330, 72, 567),
    { { DBTXTSTR, 16, 18, "How to play" },
      { DBTXTSTR, 16, 38, "Click the sea to start a wall: two arms grow apart from there." },
      { DBTXTSTR, 16, 54, "The right button or Space turns the next wall (across or down)." },
      { DBTXTSTR, 16, 70, "A buoy that hits a growing arm sinks it, and you lose a life." },
      { DBTXTSTR, 16, 86, "An arm that reaches stone sets; the sea no buoy can reach calms." },
      { DBTXTSTR, 16, 102, "Calm the goal's share of the harbour to clear the level." },
      { DBTXTSTR, 16, 122, "Crates: bolt quick-set walls, anchor holds the buoys, ring a life," },
      { DBTXTSTR, 16, 138, "star points; wall them in to take them. No tide left: a storm." },
      { DBTXTSTR, 16, 158, "Keys: P pause, Alt+N new, Alt+H scores, Alt+S sound, Alt+Q quit." } },
    { OK, 28, 172 },
    0
};

/*---------------------------------------------------------------- scores */

#define NSCORES 5
struct entry { char name[11]; u8 level; u32 score; };
static struct entry table[NSCORES];
static const char scores_file[] = "Harbour Walls Scores";
static const char magic[4] = { 'H', 'W', 'S', '1' };
static char name[11];
static char lines[NSCORES][48];
static char newline_txt[40];
static u8 fresh = 0xFF;

static char *num(char *p, u32 v)
{
    char d[11];
    u8 n = 0;
    do {
        d[n++] = (char)('0' + (u8)(v % 10));
        v /= 10;
    } while (v);
    while (n)
        *p++ = d[--n];
    *p = 0;
    return p;
}

void scores_load(void)
{
    u8 h;
    static u8 buf[4 + sizeof table];
    memset(table, 0, sizeof table);
    vr_cd(VR_DATA);
    h = FOpen((void *)scores_file, 'r');
    if (h >= 2) {
        if (FRead(h, buf, sizeof buf) == sizeof buf && !memcmp(buf, magic, 4))
            memcpy(table, buf + 4, sizeof table);
        FClose(h);
    }
    vr_cdback();
}

static u8 scores_save(void)
{
    u8 h, ok = 0;
    static u8 buf[4 + sizeof table];
    memcpy(buf, magic, 4);
    memcpy(buf + 4, table, sizeof table);
    vr_cd(VR_DATA);
    h = FOpen((void *)scores_file, 'w');
    if (h >= 2) {
        ok = FWrite(h, buf, sizeof buf) == 0;
        FClose(h);
    }
    vr_cdback();
    return ok;
}

static const struct {
    struct boxhead h;
    struct txt t[NSCORES + 2];
    struct btn ok;
    u8 end;
} scores_box = {
    HEAD(150, 330, 176, 463),
    { { DBTXTSTR, 16, 20, "High scores" },
      { DBTXTSTR, 24, 44, lines[0] },
      { DBTXTSTR, 24, 62, lines[1] },
      { DBTXTSTR, 24, 80, lines[2] },
      { DBTXTSTR, 24, 98, lines[3] },
      { DBTXTSTR, 24, 116, lines[4] },
      { DBTXTSTR, 16, 140, newline_txt } },
    { OK, 13, 152 },
    0
};

void show_scores(void)
{
    u8 i;
    char *p;
    for (i = 0; i < NSCORES; ++i) {
        p = lines[i];
        *p++ = (char)('1' + i);
        *p++ = '.';
        *p++ = ' ';
        if (table[i].score) {
            strcpy(p, table[i].name);
            p += strlen(p);
            strcpy(p, "  ");
            p = num(p + 2, table[i].score);
            strcpy(p, "  (level ");
            p = num(p + 9, table[i].level);
            strcpy(p, i == fresh ? ")  <- new" : ")");
        } else
            strcpy(p, "-");
    }
    if (fresh == 0xFF)
        strcpy(newline_txt, "Walls well built are remembered.");
    else
        strcpy(newline_txt, "Saved in /VERANDA/.SYSTEM.");
    hw_scores();
    box(&scores_box);
}

static char ask_txt[40];
static const struct {
    struct boxhead h;
    struct txt t[2];
    u8 gcmd, gx, gy, greg, gmax;
    struct btn ok;
    u8 end;
} name_box = {
    HEAD(170, 280, 176, 463),
    { { DBTXTSTR, 16, 20, ask_txt },
      { DBTXTSTR, 16, 42, "Your name:" } },
    DBGETSTRING, 90, 34, 0x0E, 10,
    { OK, 13, 76 },
    0
};

/* after a game: a place in the table? */
void scores_enter(void)
{
    u8 i, k;
    char *p;
    fresh = 0xFF;
    if (!score || score <= table[NSCORES - 1].score)
        return;
    p = ask_txt;
    strcpy(p, "A new high score: ");
    p = num(p + 18, score);
    strcpy(p, "!");
    r6 = (u16)name;
    sprites_hide();
    DoDlgBox((void *)&name_box);
    resync();
    for (k = 0; k < NSCORES && table[k].score >= score; ++k)
        ;
    for (i = NSCORES - 1; i > k; --i)
        table[i] = table[i - 1];
    memset(table[k].name, 0, sizeof table[k].name);
    strncpy(table[k].name, name[0] ? name : "SAILOR", 10);
    table[k].level = level;
    table[k].score = score;
    fresh = k;
    hw_log2(scores_save() ? "SCORES SAVED" : "SCORES NOT SAVED", k, (u16)score);
    show_scores();
}

/*---------------------------------------------------------------- sound */

void sound_init(void)
{
    static u8 p[PREFS_SIZE];
    SysPrefs(PREFS_GET, p);
    sound_on = !(p[6] & PREFS_NOSOUND);
}

/*---------------------------------------------------------------- menus */

struct item { const char *text; u8 type; void *ptr; };
struct vmenu { u8 top, bottom; u16 left, right; u8 flags; struct item items[1]; };
#define VMENU(n) struct { u8 top, bottom; u16 left, right; u8 flags; struct item items[n]; }
#define V VERTICAL | UN_CONSTRAINED

static void m_about(void) { GotoFirstMenu(); box(&about_box); }
static void m_help(void) { GotoFirstMenu(); box(&help_box); }
static void m_new(void) { GotoFirstMenu(); do_new(); }
static void m_pause(void) { GotoFirstMenu(); do_pause(); }
static void m_scores(void) { GotoFirstMenu(); fresh = 0xFF; show_scores(); }
static void m_quit(void) { GotoFirstMenu(); do_quit(); }
static void m_turn(void) { GotoFirstMenu(); toggle_orient(); }
static void m_sound(void);

static VMENU(2) appMenu = { 15, 0, 0, 0, V | 2, {
    { "About Harbour Walls", MENU_ACTION, m_about },
    { "How to play", MENU_ACTION, m_help } } };
static VMENU(4) gameMenu = { 15, 0, 0, 0, V | 4, {
    { "New game", MENU_ACTION, m_new },
    { "Pause", MENU_ACTION, m_pause },
    { "High scores", MENU_ACTION, m_scores },
    { "Quit", MENU_ACTION, m_quit } } };
static VMENU(2) optMenu = { 15, 0, 0, 0, V | 2, {
    { "Sound: on", MENU_ACTION, m_sound },
    { "Turn the wall", MENU_ACTION, m_turn } } };
#define NMAIN 3
static VMENU(NMAIN) mainMenu = { 0, 14, 0, 0, HORIZONTAL | UN_CONSTRAINED | NMAIN, {
    { "Harbour Walls", SUB_MENU, &appMenu },
    { "Game", SUB_MENU, &gameMenu },
    { "Options", SUB_MENU, &optMenu } } };

static u16 textW(const char *s)
{
    u16 w = 0;
    while (*s)
        w += GetCharWidth(*s++);
    return w;
}

/* a vertical menu's size from its items (as the shell: the kernel places
   it under its bar item, its width is the table's) */
static void sizeMenu(struct vmenu *m, u16 left)
{
    u8 i, n = m->flags & 31;
    u16 w, wmax = textW("Sound: off");
    for (i = 0; i < n; ++i) {
        w = textW(m->items[i].text);
        if (w > wmax)
            wmax = w;
    }
    m->left = left;
    m->right = left + wmax + 8;
    m->bottom = m->top + n * (curHeight + 5);
}

static void m_sound(void)
{
    GotoFirstMenu();
    toggle_sound();
}

void toggle_sound(void)
{
    sound_on ^= 1;
    optMenu.items[0].text = sound_on ? "Sound: on" : "Sound: off";
    if (!sound_on)
        sfx_off();
    else
        plat_sfx(SFX_PLACE);
    hw_log2("SOUND", sound_on, 0);
}

void menus_init(void)
{
    u8 i;
    u16 b = 0;
    UseSystemFont();
    if (!sound_on)
        optMenu.items[0].text = "Sound: off";
    for (i = 0; i < NMAIN; ++i) {
        sizeMenu((struct vmenu *)mainMenu.items[i].ptr, b);
        b += 8 + textW(mainMenu.items[i].text);
    }
    mainMenu.right = b;
    DoMenu((void *)&mainMenu, 0);
}
