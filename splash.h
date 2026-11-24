#ifndef SPLASH_H
#define SPLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* (Re)loads the image named by px_settings.splash_image from /SPLASH --
 * at boot, and after USB Disk mode (the file may have changed). Falls back
 * to the built-in image if it's unset, missing or can't be decoded. */
void splash_reload(void);

/* For choosing an image in the settings menu: load one (name without
 * ".png", or NULL for the built-in image) without changing the current
 * choice; draw it as it would look while off (into the framebuffer, not
 * flushed); and make it the current image. preview_load returns false with
 * a reason if the file can't be decoded. */
bool splash_preview_load(const char *name, char *error, size_t error_size);
void splash_preview_draw(void);
void splash_preview_use(void);

/* Draws the splash with the current date/time once, and slows the panel to
 * 1Hz -- for a deep power-off, where the panel then keeps showing it on
 * its own. */
void splash_show_static(void);

/* Redraws just the date/time bar, over the splash image still in the
 * framebuffer (retained.h) -- on the once-a-minute wakes while off
 * (power.c), after display_attach(). Nothing without Show Time
 * (px_settings.splash_clock). */
void splash_update_clock(void);

#ifdef __cplusplus
}
#endif

#endif
