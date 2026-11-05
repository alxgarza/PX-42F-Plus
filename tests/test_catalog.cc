/* The Catalog's PX42 page, by pressing keys on the real Plus42 core: the pages,
 * the tabs, and picking every command to run it, to enter it in a program and to
 * ASSIGN it. The commands themselves are stand-ins here that say they ran. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include "core_display.h"
#include "core_globals.h"
#include "core_helpers.h"
#include "core_main.h"
#include "core_variables.h"
#include "px_commands.h"
#include "shell.h"
#include "shell_spool.h"

static std::string ran;
static int failures = 0;

#define RAN(fn, name) int fn(arg_struct *) { ran = name; return ERR_NONE; }
RAN(docmd_clkt, "CLKT") RAN(docmd_clktd, "CLKTD") RAN(docmd_clock, "CLOCK") RAN(docmd_correct, "CORRECT")
RAN(docmd_rclaf, "RCLAF") RAN(docmd_setaf, "SETAF") RAN(docmd_t_plus_x, "T+X")
RAN(docmd_almcat, "ALMCAT") RAN(docmd_almnow, "ALMNOW") RAN(docmd_xyzalm, "XYZALM") RAN(docmd_clalma, "CLALMA")
RAN(docmd_clalmx, "CLALMX") RAN(docmd_clralms, "CLRALMS") RAN(docmd_rclalm, "RCLALM")
RAN(docmd_rclsw, "RCLSW") RAN(docmd_runsw, "RUNSW") RAN(docmd_setsw, "SETSW") RAN(docmd_stopsw, "STOPSW")
RAN(docmd_sw, "SW") RAN(docmd_swpt, "SWPT")
RAN(docmd_playwav, "PLAYWAV") RAN(docmd_playmid, "PLAYMID") RAN(docmd_stopsnd, "STOPSND") RAN(docmd_sndwait, "SNDWAIT")
RAN(docmd_pen, "PEN") RAN(docmd_box, "BOX") RAN(docmd_dline, "DLINE") RAN(docmd_ellipse, "ELLIPSE") RAN(docmd_polygon, "POLYGON")

bool shell_set_date(int4, int4, int4) { ran = "SETDATE"; return true; }
void shell_set_time(int4, int4, int4) { ran = "SETIME"; }

extern "C" void free42_forget_shapes() {}
bool px_graphics_memory() { return true; }
void px_pixel(phloat, phloat) {}
void px_pattern(phloat, phloat, const char *, int) {}
FILE *my_fopen(const char *, const char *) { return NULL; }
int my_rename(const char *, const char *) { return -1; }
int my_remove(const char *) { return -1; }
int hp2ascii(char *, const char *, int) { return 0; }
const char *shell_platform() { return "test"; }
void shell_blitter(const char *, int, int, int, int, int) {}
void shell_beeper(int) {}
void shell_annunciators(int, int, int, int, int, int) {}
bool shell_wants_cpu() { return false; }
void shell_delay(int) {}
void shell_request_timeout3(int) {}
void shell_request_display_size(int, int) {}
uint8 shell_get_mem() { return 1000000; }
bool shell_low_battery() { return false; }
void shell_powerdown() {}
int8 shell_random_seed() { return 1; }
uint4 shell_milliseconds() { return 0; }
const char *shell_number_format() { return ".,3"; }
void shell_set_skin_mode(int) {}
int shell_date_format() { return 1; }
bool shell_clk24() { return true; }
void shell_print(const char *, int, const char *, int, int, int, int, int) {}
void shell_get_time_date(uint4 *time, uint4 *date, int *weekday) { *time = 0; *date = 20260101; *weekday = 4; }
void shell_message(const char *) {}

static void press(int key) {
    bool enqueued;
    int repeat;
    bool more = core_keydown(key, &enqueued, &repeat);
    while (more)
        more = core_keydown(0, &enqueued, &repeat);
    more = core_keyup();
    while (more)
        more = core_keydown(0, &enqueued, &repeat);
}

static void shifted(int key) {
    press(KEY_SHIFT);
    press(key);
}

static std::string keys() {
    std::string out;
    for (int n = 0; n < 6; n++) {
        const char *text = "";
        int length = 0;
        bool highlight, reverse;
        display_key_label(n, &text, &length, &highlight, &reverse);
        if (n > 0)
            out += ' ';
        out += length == 0 ? "-" : std::string(text, length);
    }
    return out;
}

static void check(const char *what, const std::string &got, const std::string &want) {
    if (got != want) {
        printf("FAILED %s\n  got  %s\n  want %s\n", what, got.c_str(), want.c_str());
        failures++;
    }
}

static void close_menus() {
    for (int i = 0; i < 6; i++)
        press(KEY_EXIT);
    check("everything closed", get_front_menu() == MENU_CATALOG ? "catalog open" : "closed", "closed");
}

/* From the first page: up one page to PX42. */
static void open_px_page() {
    shifted(KEY_ADD);
    press(KEY_UP);
}

/* Each tab's rows, each row's keys; code is the command's XROM code. */
struct cell { const char *name; int code; int row; int key; };
struct tab { const char *rows[2]; int n; cell cells[9]; };
static const tab tabs[5] = {
    { { "CLOCK CLKT CLKTD SDATE STIME T+X", "CORCT RCLAF SETAF - - -" }, 9, {
        { "CLOCK", 0xa68a, 0, 0 }, { "CLKT", 0xa688, 0, 1 }, { "CLKTD", 0xa689, 0, 2 },
        { "SETDATE", 0xa696, 0, 3 }, { "SETIME", 0xa697, 0, 4 }, { "T+X", 0xa69b, 0, 5 },
        { "CORRECT", 0xa68b, 1, 0 }, { "RCLAF", 0xa692, 1, 1 }, { "SETAF", 0xa695, 1, 2 } } },
    { { "XYZAL ALCAT ALNOW RCALM CLMA CLMX", "CLRMS - - - - -" }, 7, {
        { "XYZALM", 0xa69d, 0, 0 }, { "ALMCAT", 0xa682, 0, 1 }, { "ALMNOW", 0xa683, 0, 2 },
        { "RCLALM", 0xa6a2, 0, 3 }, { "CLALMA", 0xa69f, 0, 4 }, { "CLALMX", 0xa6a0, 0, 5 },
        { "CLRALMS", 0xa6a1, 1, 0 } } },
    { { "SW SWPT RUNSW STPSW SETSW RCLSW", NULL }, 6, {
        { "SW", 0xa69a, 0, 0 }, { "SWPT", 0xa6a3, 0, 1 }, { "RUNSW", 0xa694, 0, 2 },
        { "STOPSW", 0xa699, 0, 3 }, { "SETSW", 0xa698, 0, 4 }, { "RCLSW", 0xa693, 0, 5 } } },
    { { "PLWAV PLMID STSND SNDWT - -", NULL }, 4, {
        { "PLAYWAV", 0xa001, 0, 0 }, { "PLAYMID", 0xa002, 0, 1 }, { "STOPSND", 0xa003, 0, 2 },
        { "SNDWAIT", 0xa004, 0, 3 } } },
    { { "PEN BOX DLINE ELIPS PGON -", NULL }, 5, {
        { "PEN", 0xa005, 0, 0 }, { "BOX", 0xa006, 0, 1 }, { "DLINE", 0xa007, 0, 2 },
        { "ELLIPSE", 0xa008, 0, 3 }, { "POLYGON", 0xa009, 0, 4 } } },
};
static const char *const PX_PAGE = "CLOCK ALARM SW SOUND DRAW -";

static void pick(int t, const cell &c) {
    open_px_page();
    press(KEY_SIGMA + t);
    if (c.row == 1)
        press(KEY_DOWN);
    press(KEY_SIGMA + c.key);
}

static void test_pages() {
    shifted(KEY_ADD);
    check("first page", keys(), "FCN PGM REAL CPX MAT MEM");
    check("second page", (press(KEY_DOWN), keys()), "LIST EQN EQN OTHER DIRS UNITS");
    check("third page", (press(KEY_DOWN), keys()), "TIME XFCN BASE PRGM STR STK");
    check("fourth page", (press(KEY_DOWN), keys()), "EQNS UNIT STAT DIR DISP MISC");
    check("down to PX42", (press(KEY_DOWN), keys()), PX_PAGE);
    check("down back to the first", (press(KEY_DOWN), keys()), "FCN PGM REAL CPX MAT MEM");
    check("up to PX42", (press(KEY_UP), keys()), PX_PAGE);
    check("up to the fourth", (press(KEY_UP), keys()), "EQNS UNIT STAT DIR DISP MISC");
    close_menus();
}

static void test_tabs() {
    for (int t = 0; t < 5; t++) {
        open_px_page();
        press(KEY_SIGMA + t);
        check("tab's first row", keys(), tabs[t].rows[0]);
        if (tabs[t].rows[1] != NULL) {
            check("tab's second row", (press(KEY_DOWN), keys()), tabs[t].rows[1]);
            check("rows wrap", (press(KEY_DOWN), keys()), tabs[t].rows[0]);
            check("up to the last row", (press(KEY_UP), keys()), tabs[t].rows[1]);
        }
        check("EXIT goes back to the page", (press(KEY_EXIT), keys()), PX_PAGE);
        close_menus();
    }
    open_px_page();
    press(KEY_SIGMA + 5);
    check("the empty tab does nothing", keys(), PX_PAGE);
    close_menus();
}

static void test_run() {
    int commands = 0;
    for (int t = 0; t < 5; t++)
        for (int i = 0; i < tabs[t].n; i++) {
            const cell &c = tabs[t].cells[i];
            vartype *x = strcmp(c.name, "POLYGON") == 0 ? new_realmatrix(1, 2) : new_real(1.012026);
            free_vartype(stack[sp]);
            stack[sp] = x;
            ran = "nothing";
            pick(t, c);
            check("picking it runs it", ran, c.name);
            check("and takes the menu away", keys(), "- - - - - -");
            close_menus();
            commands++;
        }
    check("every command", std::to_string(commands), "31");
}

static void test_program() {
    shifted(KEY_RUN); /* PRGM */
    for (int t = 0; t < 5; t++)
        for (int i = 0; i < tabs[t].n; i++) {
            pick(t, tabs[t].cells[i]);
            for (int e = 0; e < 3 && get_front_menu() == MENU_CATALOG; e++)
                press(KEY_EXIT);
        }
    shifted(KEY_RUN);
    int4 pc = 0;
    int cmd;
    arg_struct arg;
    for (int t = 0; t < 5; t++)
        for (int i = 0; i < tabs[t].n; i++) {
            const cell &c = tabs[t].cells[i];
            do
                get_next_command(&pc, &cmd, &arg, 0, NULL);
            while (cmd == CMD_LBL);
            /* The listing's full name, whatever the soft key leaves out. */
            char line[32], got[48], want[32];
            int length = command2buf(line, sizeof line, cmd, &arg);
            snprintf(got, sizeof got, "%.*s %02x%02x", length, line, cmd_array[cmd].code1, cmd_array[cmd].code2);
            snprintf(want, sizeof want, "%s %04x", c.name, c.code);
            check("the program line", got, want);
            check("XEQ finds the full name", cmd_array[find_builtin(c.name, strlen(c.name))].code2 == (c.code & 0xff)
                  ? "found" : "not found", "found");
        }
}

static void test_assign() {
    shifted(KEY_1); /* ASSIGN */
    press(KEY_UP);
    check("ASSIGN: the page", keys(), PX_PAGE);
    press(KEY_SIGMA);
    check("ASSIGN: a tab", keys(), tabs[0].rows[0]);
    check("ASSIGN: EXIT", (press(KEY_EXIT), keys()), PX_PAGE);
    press(KEY_SIGMA);
    check("ASSIGN: backspace", (press(KEY_BSP), keys()), PX_PAGE);
    press(KEY_SIGMA);
    press(KEY_SIGMA + 3); /* SETDATE */
    press(KEY_SIGMA);     /* to the first CUSTOM key */
    char name[8];
    int length;
    get_custom_key(1, false, name, &length);
    for (int i = 0; i < length; i++)
        name[i] &= 127;
    check("ASSIGN: the CUSTOM key", std::string(name, length), "SETDATE");
    close_menus();
    set_menu(MENULEVEL_AUX, MENU_CUSTOM1);
    redisplay();
    check("CUSTOM: the short name", keys().substr(0, 5), "SDATE");
    free_vartype(stack[sp]);
    stack[sp] = new_real(1.012026);
    ran = "nothing";
    press(KEY_SIGMA);
    check("CUSTOM: the key runs it", ran, "SETDATE");
    close_menus();
}

int main() {
    int rows, cols;
    core_init(&rows, &cols, NULL);
    skin_flags = 3; /* the PX's: CATALOG's first row is FCN ... MEM */
    test_pages();
    test_tabs();
    test_run();
    test_program();
    test_assign();
    if (failures > 0) {
        printf("Catalog: %d FAILED\n", failures);
        return 1;
    }
    puts("Catalog: all passed");
    return 0;
}
