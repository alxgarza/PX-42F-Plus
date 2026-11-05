#ifndef PX_COMMANDS_H
#define PX_COMMANDS_H 1

#include "free42.h"
#include "core_globals.h"
#include "stopwatch.h"

/* The panel's size: what Plus42's graphics use on the PX, its own bitmap being
 * another size for each number of rows, and redrawn in fonts (free42_app.cc). */
#define PX_GRAPHICS_W 250
#define PX_GRAPHICS_H 122

/* PIXEL's and AGRAPH's drawing, on the panel (PX42 patches in pixel_helper() and
 * draw_pattern()); px_graphics_memory() before them, as they can't fail. */
bool px_graphics_memory();
void px_pixel(phloat dx, phloat dy);
void px_pattern(phloat dx, phloat dy, const char *pattern, int width);

/* The PX's own commands, added to Plus42's table by tools/px_core_tables.py. */
int docmd_pen(arg_struct *arg);
int docmd_box(arg_struct *arg);
int docmd_dline(arg_struct *arg);
int docmd_ellipse(arg_struct *arg);
int docmd_polygon(arg_struct *arg);
int docmd_playwav(arg_struct *arg);
int docmd_playmid(arg_struct *arg);
int docmd_stopsnd(arg_struct *arg);
int docmd_clkt(arg_struct *arg);
int docmd_clktd(arg_struct *arg);
int docmd_clock(arg_struct *arg);
int docmd_correct(arg_struct *arg);
int docmd_rclaf(arg_struct *arg);
int docmd_setaf(arg_struct *arg);
int docmd_t_plus_x(arg_struct *arg);
int docmd_almcat(arg_struct *arg);
int docmd_almnow(arg_struct *arg);
int docmd_xyzalm(arg_struct *arg);
int docmd_clalma(arg_struct *arg);
int docmd_clalmx(arg_struct *arg);
int docmd_clralms(arg_struct *arg);
int docmd_rclalm(arg_struct *arg);
int docmd_sndwait(arg_struct *arg);

int docmd_rclsw(arg_struct *arg);
int docmd_runsw(arg_struct *arg);
int docmd_setsw(arg_struct *arg);
int docmd_stopsw(arg_struct *arg);
int docmd_sw(arg_struct *arg);
int docmd_swpt(arg_struct *arg);

/* The data registers, for the stopwatch's splits (free42_app.cc). */
const stopwatch_regs_t *px_stopwatch_registers();

#endif
