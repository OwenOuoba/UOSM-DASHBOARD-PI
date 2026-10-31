/**
 * screen.h — the LVGL screen on the Raspberry Pi (Linux framebuffer /dev/fb0).
 *
 * main.c only calls these three functions, from its single loop (option A):
 *   screen_init()      once at startup
 *   screen_update(st)  every 100 ms, with the state built by the core
 *   screen_tick()      every loop turn: LVGL draws what changed
 * What is drawn is in ui.c; this file only connects LVGL to the Pi's screen.
 */
#ifndef SCREEN_H
#define SCREEN_H

#include "ui.h"

/** Starts LVGL on /dev/fb0 and builds the screen. Returns false if the screen cannot be opened. */
bool screen_init(void);

/** Shows a new state (calls ui_update). */
void screen_update(const dashboard_state_t* state);

/** Lets LVGL run its timers and redraw what changed. Call at every loop turn. */
void screen_tick(void);

#endif /* SCREEN_H */
