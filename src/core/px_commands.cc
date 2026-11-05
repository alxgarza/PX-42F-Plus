#include <string.h>
#include "px_commands.h"
#include "alarms.h"
#include "core_commands2.h"
#include "core_display.h"
#include "core_helpers.h"
#include "core_main.h"
#include "free42_app.h"
#include "px_draw.h"
#include "px_layer.h"
#include "rtc.h"
#include "settings.h"
#include "sounds.h"
#include "soundname.h"
#include "stopwatch.h"
#include "timemodule.h"

// /SOUNDS and /MIDI hold ALPHA's 44 characters and the folder.
#define SOUND_PATH_MAX 64

// PLAYWAV and PLAYMID start the sound and go on at once, so a program can draw
// or read keys meanwhile; SNDWAIT is how it waits instead. No key sound cuts
// them, and R/S stops them (free42_key_down()). Flag 26 silences both, as it
// does TONE and BEEP.

int docmd_playwav(arg_struct *arg) {
    char path[SOUND_PATH_MAX];
    if (!sound_name_path(path, sizeof(path), "/SOUNDS", ".wav", true, reg_alpha, reg_alpha_length))
        return ERR_ALPHA_DATA_IS_INVALID;
    if (!flags.f.audio_enable)
        return ERR_NONE;
    return sounds_play_file(path, false) ? ERR_NONE : ERR_NONEXISTENT;
}

int docmd_playmid(arg_struct *arg) {
    char path[SOUND_PATH_MAX];
    if (!sound_name_path(path, sizeof(path), "/MIDI", ".mid", false, reg_alpha, reg_alpha_length))
        return ERR_ALPHA_DATA_IS_INVALID;
    if (!flags.f.audio_enable)
        return ERR_NONE;
    switch (sounds_midi_play(path)) {
        case 0:
            return ERR_NONE;
        case SOUNDS_MIDI_NOT_FOUND:
            return ERR_NONEXISTENT;
        case SOUNDS_MIDI_NOT_MIDI:
            return ERR_INVALID_DATA;
        default:
            return ERR_INSUFFICIENT_MEMORY;
    }
}

int docmd_stopsnd(arg_struct *arg) {
    sounds_stop();
    return ERR_NONE;
}

// Interruptible, so a long sound can't hold the keys: R/S and EXIT stop it, and
// the program.
static int sndwait_worker(bool interrupted) {
    if (interrupted) {
        sounds_stop();
        return ERR_STOP;
    }
    return sounds_file_playing() ? ERR_INTERRUPTIBLE : ERR_NONE;
}

int docmd_sndwait(arg_struct *arg) {
    if (!sounds_file_playing())
        return ERR_NONE;
    mode_interruptible = sndwait_worker;
    mode_stoppable = true;
    return ERR_INTERRUPTIBLE;
}

// The rest of the HP-41 Time Module's clock, as its manual has them (HP-41CX
// owner's manual, volume 2, section 15 and appendix F).

int docmd_clkt(arg_struct *arg) {
    px_settings.clock_with_date = false;
    settings_save_later();
    return ERR_NONE;
}

int docmd_clktd(arg_struct *arg) {
    px_settings.clock_with_date = true;
    settings_save_later();
    return ERR_NONE;
}

// In a program it stops there, R/S going on: a running program's own display
// would replace the clock at once.
int docmd_clock(arg_struct *arg) {
    free42_clock_start();
    return program_running() ? ERR_STOP : ERR_NONE;
}

static bool x_to_ms(phloat x, int64_t *ms) {
    return time_digits_to_ms(to_int8(floor(x * 1000000)), ms);
}

// Sets the time and corrects the rate by the drift since the clock was last
// set exactly, as Settings' Time Calibration does: the same measurement, which
// needs 6 hours and an error under 30 minutes (rtc.c), or only the time changes.
int docmd_correct(arg_struct *arg) {
    phloat x = ((vartype_real *) stack[sp])->x;
    int64_t ms;
    if (x < 0 || x >= 24 || !x_to_ms(x, &ms))
        return ERR_INVALID_DATA;
    rtc_set_ms_of_day((uint32_t) ms);
    if (rtc_set_measured()) {
        rtc_set_trim(px_settings.time_trim - px_settings.cal_drift);
        px_settings.cal_span_h = 0; // used
    }
    settings_save_later();
    return ERR_NONE;
}

int docmd_rclaf(arg_struct *arg) {
    vartype *v = new_real(phloat(trim_to_af_tenths(px_settings.time_trim)) / 10);
    if (v == NULL)
        return ERR_INSUFFICIENT_MEMORY;
    return recall_result(v);
}

int docmd_setaf(arg_struct *arg) {
    phloat x = ((vartype_real *) stack[sp])->x;
    phloat size = x < 0 ? -x : x;
    int af = x == 0 || size >= 100 ? 0 : af_tenths_from_milli(to_int8(floor(size * 1000)));
    rtc_set_trim(af_tenths_to_trim(x < 0 ? -af : af));
    rtc_measure_from_now();
    settings_save_later();
    return ERR_NONE;
}

// +HHHH.MMSSss, into another day if it comes to that. The rate's measurement
// moves with it: a time zone isn't drift.
int docmd_t_plus_x(arg_struct *arg) {
    phloat x = ((vartype_real *) stack[sp])->x;
    phloat size = x < 0 ? -x : x;
    int64_t ms;
    if (size >= 10000 || !x_to_ms(size, &ms))
        return ERR_INVALID_DATA;
    return rtc_add_ms(x < 0 ? -ms : ms) ? ERR_NONE : ERR_INVALID_DATA;
}

// The HP-41 Time Module's alarms (HP-41CX owner's manual, volume 2, section 16
// and appendix B). What they do when they come due: alarm_service(), free42_app.cc.

static time_date_order_t date_order() {
    return flags.f.ymd ? TIME_YMD : flags.f.dmy ? TIME_DMY : TIME_MDY;
}

static int64_t digits_of(phloat x) {
    return to_int8(floor((x < 0 ? -x : x) * 1000000));
}

static void reminder_tones() {
    if (flags.f.audio_enable) {
        sounds_play_tone(8, true);
        sounds_play_tone(9, false);
    }
}

// X time, Y date (0 = today), Z repeat (0 = none), ALPHA the message, or ↑↑LABEL
// or ↑LABEL. A label must be plain ASCII: the shell runs it by name
// (core_keydown_command()), which takes no other.
int docmd_xyzalm(arg_struct *arg) {
    phloat x = ((vartype_real *) stack[sp])->x;
    phloat y = ((vartype_real *) stack[sp - 1])->x;
    phloat z = ((vartype_real *) stack[sp - 2])->x;

    int64_t ms_of_day, repeat_ms = 0;
    if (x >= 24 || x <= -12 || !alarm_time_to_ms(digits_of(x), x < 0, &ms_of_day))
        return ERR_INVALID_DATA;
    if (z != 0) {
        if (z < 0 || z >= 10000 || !time_digits_to_ms(digits_of(z), &repeat_ms) || repeat_ms < 1000)
            return ERR_INVALID_DATA;
    }
    uint64_t now = rtc_now_ms();
    int64_t days = (int64_t) (now / 86400000);
    if (y != 0 && (y < 0 || y >= 10000 || !alarm_date_to_days(digits_of(y), date_order(), &days)))
        return ERR_INVALID_DATA;

    alarm_t probe;
    probe.length = (uint8_t) reg_alpha_length;
    memcpy(probe.text, reg_alpha, (size_t) reg_alpha_length);
    if (!alarm_label_ok(&probe))
        return ERR_ALPHA_DATA_IS_INVALID;

    uint64_t at = (uint64_t) days * 86400000 + (uint64_t) ms_of_day;
    if (!alarms_add(at, (uint64_t) repeat_ms, reg_alpha, reg_alpha_length, now))
        return ERR_INSUFFICIENT_MEMORY;
    if (at <= now)
        reminder_tones();
    return ERR_NONE;
}

int docmd_clralms(arg_struct *arg) {
    alarms_clear();
    return ERR_NONE;
}

int docmd_clalma(arg_struct *arg) {
    int i = alarms_find_text(reg_alpha, reg_alpha_length);
    if (i < 0)
        return ERR_NONEXISTENT;
    alarms_remove(i);
    return ERR_NONE;
}

// The alarm numbered by X, as the catalog lists them from 1. 0 or over 999 is
// bad data, one that isn't there doesn't exist.
static int alarm_number(int *i) {
    phloat x = ((vartype_real *) stack[sp])->x;
    if (x < 0)
        x = -x;
    if (x >= 1000 || x < 1)
        return ERR_INVALID_DATA;
    *i = to_int(x) - 1;
    return *i < alarms_count() ? ERR_NONE : ERR_NONEXISTENT;
}

int docmd_clalmx(arg_struct *arg) {
    int i;
    int err = alarm_number(&i);
    if (err != ERR_NONE)
        return err;
    alarms_remove(i);
    return ERR_NONE;
}

// X the time as HH.MMSSss, Y the date, Z the repeat, ALPHA the text; the old X in
// LAST X and the old Y lifted to T, as the manual has it.
int docmd_rclalm(arg_struct *arg) {
    int i;
    int err = alarm_number(&i);
    if (err != ERR_NONE)
        return err;
    const alarm_t *a = alarms_get(i);
    vartype *time = new_real(phloat((int8) time_ms_to_digits(a->at_ms % 86400000)) / 1000000);
    vartype *date = new_real(phloat((int8) alarm_days_to_digits((int64_t) (a->at_ms / 86400000), date_order())) / 1000000);
    vartype *repeat = new_real(phloat((int8) time_ms_to_digits(a->repeat_ms)) / 1000000);
    if (time == NULL || date == NULL || repeat == NULL) {
        free_vartype(time);
        free_vartype(date);
        free_vartype(repeat);
        return ERR_INSUFFICIENT_MEMORY;
    }
    reg_alpha_length = a->length;
    memcpy(reg_alpha, a->text, a->length);
    unary_result(repeat);
    recall_result(date);
    return recall_result(time);
}

// The oldest past-due alarm with a program, run now; in a program, as a subroutine.
int docmd_almnow(arg_struct *arg) {
    int i = alarms_past_due_program();
    if (i < 0)
        return ERR_NONE;
    int length;
    const char *label = alarm_label(alarms_get(i), &length);
    arg_struct target;
    target.type = ARGTYPE_STR;
    target.length = (unsigned short) length;
    memcpy(target.val.text, label, (size_t) length);
    alarms_done(i, rtc_now_ms());
    return length == 0 ? ERR_NONE : docmd_xeq(&target);
}

// Like CLOCK, in a program it stops there: R/S goes on.
int docmd_almcat(arg_struct *arg) {
    free42_almcat_start();
    return program_running() ? ERR_STOP : ERR_NONE;
}

// The stopwatch (HP-41CX owner's manual, volume 2, section 17). stopwatch.c has its
// rules, free42_app.cc its keys and its screen.

// A time in a register. REGS as STO and RCL have it: a real or complex matrix, or a list.
static int sw_reg_size() {
    vartype *regs = recall_var("REGS", 4);
    if (regs == NULL)
        return 0;
    switch (regs->type) {
        case TYPE_REALMATRIX:
            return ((vartype_realmatrix *) regs)->rows * ((vartype_realmatrix *) regs)->columns;
        case TYPE_COMPLEXMATRIX:
            return ((vartype_complexmatrix *) regs)->rows * ((vartype_complexmatrix *) regs)->columns;
        case TYPE_LIST:
            return ((vartype_list *) regs)->size;
        default:
            return 0;
    }
}

static stopwatch_reg_t sw_reg_get(int n, int64_t *digits) {
    if (n < 0 || n >= sw_reg_size())
        return STOPWATCH_REG_MISSING;
    vartype *regs = recall_var("REGS", 4);
    phloat x;
    if (regs->type == TYPE_REALMATRIX) {
        vartype_realmatrix *m = (vartype_realmatrix *) regs;
        if (m->array->is_string[n] != 0)
            return STOPWATCH_REG_NOT_TIME;
        x = m->array->data[n];
    } else if (regs->type == TYPE_LIST) {
        vartype *v = ((vartype_list *) regs)->array->data[n];
        if (v->type != TYPE_REAL)
            return STOPWATCH_REG_NOT_TIME;
        x = ((vartype_real *) v)->x;
    } else {
        return STOPWATCH_REG_NOT_TIME;
    }
    phloat size = x < 0 ? -x : x;
    if (size >= 100)
        return STOPWATCH_REG_NOT_TIME;
    int64_t d = to_int8(floor(size * 1000000));
    *digits = x < 0 ? -d : d;
    return STOPWATCH_REG_OK;
}

static bool sw_reg_put(int n, int64_t digits) {
    if (n < 0 || n >= sw_reg_size())
        return false;
    vartype *regs = recall_var("REGS", 4);
    phloat x = phloat((int8) digits) / 1000000;
    if (!disentangle(regs))
        return false;
    if (regs->type == TYPE_REALMATRIX) {
        vartype_realmatrix *m = (vartype_realmatrix *) regs;
        if (m->array->is_string[n] == 2)
            free(*(void **) &m->array->data[n]);
        m->array->is_string[n] = 0;
        m->array->data[n] = x;
    } else if (regs->type == TYPE_COMPLEXMATRIX) {
        vartype_complexmatrix *m = (vartype_complexmatrix *) regs;
        m->array->data[2 * n] = x;
        m->array->data[2 * n + 1] = 0;
    } else {
        vartype_list *list = (vartype_list *) regs;
        vartype *v = new_real(x);
        if (v == NULL)
            return false;
        free_vartype(list->array->data[n]);
        list->array->data[n] = v;
    }
    return true;
}

const stopwatch_regs_t *px_stopwatch_registers() {
    static const stopwatch_regs_t regs = {sw_reg_size, sw_reg_get, sw_reg_put};
    return &regs;
}

int docmd_runsw(arg_struct *arg) {
    stopwatch_run(rtc_now_ms());
    return ERR_NONE;
}

int docmd_stopsw(arg_struct *arg) {
    stopwatch_stop(rtc_now_ms());
    return ERR_NONE;
}

int docmd_setsw(arg_struct *arg) {
    phloat x = ((vartype_real *) stack[sp])->x;
    phloat size = x < 0 ? -x : x;
    int64_t ms;
    if (size >= 100 || !x_to_ms(size, &ms))
        return ERR_INVALID_DATA;
    stopwatch_set(x < 0 ? -ms : ms, rtc_now_ms());
    return ERR_NONE;
}

int docmd_rclsw(arg_struct *arg) {
    int64_t ms = stopwatch_ms(rtc_now_ms());
    int64_t digits = time_ms_to_digits((uint64_t) (ms < 0 ? -ms : ms));
    vartype *v = new_real(phloat((int8) (ms < 0 ? -digits : digits)) / 1000000);
    if (v == NULL)
        return ERR_INSUFFICIENT_MEMORY;
    return recall_result(v);
}

// The keyboard takes over until EXIT, and then a program goes on, which is what the
// manual's SPLITS program counts on. So the core waits here (ERR_INTERRUPTIBLE)
// without being called: free42_service() leaves it alone until the keyboard ends.
static bool sw_returns_pointers;

static int sw_worker(bool interrupted) {
    if (interrupted) { // OFF, or the memory being saved for an app
        free42_stopwatch_close();
        return ERR_STOP;
    }
    if (free42_stopwatch_showing())
        return ERR_INTERRUPTIBLE;
    int err = free42_stopwatch_error();
    if (err != ERR_NONE || !sw_returns_pointers)
        return err;
    int store, recall;
    bool delta;
    stopwatch_pointers(&store, &recall, &delta);
    phloat x = phloat((int8) (store * 1000 + recall)) / 1000;
    if (delta)
        x = store == 0 && recall == 0 ? phloat(-1) / 10000000 : -x;
    vartype *v = new_real(x);
    if (v == NULL)
        return ERR_INSUFFICIENT_MEMORY;
    unary_result(v);
    return ERR_NONE;
}

static int sw_begin(int store, int recall, bool delta, bool returns_pointers) {
    sw_returns_pointers = returns_pointers;
    free42_stopwatch_open(store, recall, delta);
    mode_interruptible = sw_worker;
    mode_stoppable = false;
    return ERR_INTERRUPTIBLE;
}

int docmd_sw(arg_struct *arg) {
    return sw_begin(0, 0, false, false);
}

// X: sss.rrr, the storing and the recalling pointer; negative for delta splits.
// It comes back in X when the keyboard ends, the old one in LAST X.
int docmd_swpt(arg_struct *arg) {
    phloat x = ((vartype_real *) stack[sp])->x;
    phloat size = x < 0 ? -x : x;
    if (size >= STOPWATCH_POINTER_MAX + 1)
        return ERR_INVALID_DATA;
    int64_t pointers = to_int8(floor(size * 1000));
    return sw_begin((int) (pointers / 1000), (int) (pointers % 1000), x < 0, true);
}

// Shapes, on the panel's 250 x 122 whatever Plus42's rows are, from 1,1 at the top
// left, as PIXEL and AGRAPH count (apps count from 0,0: px_draw.c does too, so a
// position is one less there). free42_shape_canvases() has what happens to them. The handlers leave the
// stack as it is, as PIXEL and AGRAPH do. handle() has checked that the arguments
// are real numbers.

#define PEN_WIDTH_MAX 64 /* px_draw.c: the most a line takes */
#define SHAPE_COORD_MAX 4096

static int pen_width = 1; /* 0 filled, 1 one pixel, n pixels */
static int pen_black = 1;

// X the color (1 black, 0 white), Y the width.
int docmd_pen(arg_struct *arg) {
    phloat color = ((vartype_real *) stack[sp])->x;
    phloat width = ((vartype_real *) stack[sp - 1])->x;
    if ((color != 0 && color != 1) || width < 0 || width >= PEN_WIDTH_MAX + 1)
        return ERR_INVALID_DATA;
    pen_black = color == 1;
    pen_width = to_int(width);
    return ERR_NONE;
}

static int shape_coord(phloat v) {
    if (v > SHAPE_COORD_MAX)
        return SHAPE_COORD_MAX;
    if (v < -SHAPE_COORD_MAX)
        return -SHAPE_COORD_MAX;
    return to_int(floor(v + 0.5));
}

// T, Z, Y, X: a shape's four numbers.
static void four_numbers(int *n) {
    for (int i = 0; i < 4; i++)
        n[i] = shape_coord(((vartype_real *) stack[sp - 3 + i])->x);
}

static int shape_end() {
    mode_message_lines = ALL_LINES;
    free42_shapes_changed();
    return ERR_NONE;
}

int docmd_box(arg_struct *arg) {
    px_canvas_t ink, wipe;
    int n[4];
    if (!free42_shape_canvases(&ink, &wipe))
        return ERR_INSUFFICIENT_MEMORY;
    four_numbers(n);
    px_draw_box(&ink, n[0] - 1, n[1] - 1, n[2], n[3], pen_width, pen_black);
    px_draw_box(&wipe, n[0] - 1, n[1] - 1, n[2], n[3], pen_width, !pen_black);
    return shape_end();
}

int docmd_dline(arg_struct *arg) {
    px_canvas_t ink, wipe;
    int n[4];
    if (!free42_shape_canvases(&ink, &wipe))
        return ERR_INSUFFICIENT_MEMORY;
    four_numbers(n);
    px_draw_line(&ink, n[0] - 1, n[1] - 1, n[2] - 1, n[3] - 1, pen_width, pen_black);
    px_draw_line(&wipe, n[0] - 1, n[1] - 1, n[2] - 1, n[3] - 1, pen_width, !pen_black);
    return shape_end();
}

int docmd_ellipse(arg_struct *arg) {
    px_canvas_t ink, wipe;
    int n[4];
    if (!free42_shape_canvases(&ink, &wipe))
        return ERR_INSUFFICIENT_MEMORY;
    four_numbers(n);
    px_draw_ellipse(&ink, n[0] - 1, n[1] - 1, n[2], n[3], pen_width, pen_black);
    px_draw_ellipse(&wipe, n[0] - 1, n[1] - 1, n[2], n[3], pen_width, !pen_black);
    return shape_end();
}

// X a matrix of two columns, x and y, a row a corner.
int docmd_polygon(arg_struct *arg) {
    vartype_realmatrix *m = (vartype_realmatrix *) stack[sp];
    if (m->columns != 2 || m->rows < 1 || m->rows > PX_POLY_MAX)
        return ERR_DIMENSION_ERROR;
    px_point_t corners[PX_POLY_MAX];
    for (int4 i = 0; i < m->rows; i++) {
        if (m->array->is_string[2 * i] || m->array->is_string[2 * i + 1])
            return ERR_ALPHA_DATA_IS_INVALID;
        corners[i].x = (int16_t) (shape_coord(m->array->data[2 * i]) - 1);
        corners[i].y = (int16_t) (shape_coord(m->array->data[2 * i + 1]) - 1);
    }
    px_canvas_t ink, wipe;
    if (!free42_shape_canvases(&ink, &wipe))
        return ERR_INSUFFICIENT_MEMORY;
    px_draw_polygon(&ink, corners, (int) m->rows, pen_width, pen_black);
    px_draw_polygon(&wipe, corners, (int) m->rows, pen_width, !pen_black);
    return shape_end();
}

// PIXEL and AGRAPH as Plus42's own, from 1,1, on the panel. They count as shown at
// once: a matrix draws many, and each would clear the one before (a screen is
// cleared at the first drawing on it).

bool px_graphics_memory() {
    return free42_shape_memory();
}

static int round_away(phloat v) {
    if (v > SHAPE_COORD_MAX)
        return SHAPE_COORD_MAX;
    if (v < -SHAPE_COORD_MAX)
        return -SHAPE_COORD_MAX;
    return v < 0 ? to_int(-floor(-v + 0.5)) : to_int(floor(v + 0.5));
}

// Negative: a whole column, or row, as PIXEL does.
void px_pixel(phloat dx, phloat dy) {
    px_canvas_t ink, wipe;
    if (!free42_shape_canvases(&ink, &wipe))
        return;
    mode_message_lines = ALL_LINES;
    int x = round_away(dx), y = round_away(dy);
    if (x < 0)
        for (int i = 0; i < PX_GRAPHICS_H; i++)
            px_layer_dot(&ink, &wipe, -x - 1, i, 1);
    if (y < 0)
        for (int i = 0; i < PX_GRAPHICS_W; i++)
            px_layer_dot(&ink, &wipe, i, -y - 1, 1);
    if (x > 0 && y > 0)
        px_layer_dot(&ink, &wipe, x - 1, y - 1, 1);
    free42_shapes_changed();
}

void px_pattern(phloat dx, phloat dy, const char *pattern, int width) {
    px_canvas_t ink, wipe;
    if (!free42_shape_canvases(&ink, &wipe))
        return;
    mode_message_lines = ALL_LINES;
    px_pattern_mode_t mode = (px_pattern_mode_t) ((flags.f.agraph_control1 ? 2 : 0) + (flags.f.agraph_control0 ? 1 : 0));
    px_layer_pattern(&ink, &wipe, round_away(dx) - 1, round_away(dy) - 1, pattern, width, mode);
    free42_shapes_changed();
}
