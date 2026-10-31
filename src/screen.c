#include "screen.h"

#include "lvgl.h"

#include <stdio.h>
#include <time.h>
#include <unistd.h>   /* access() */

#define FRAMEBUFFER "/dev/fb0"   /* the HDMI screen, as Linux exposes it */

/* LVGL needs to know the time in milliseconds (animations, blinking, its own timers).
   Monotonic clock: it never jumps, even when the Pi sets its date at boot. */
static uint32_t tick_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    /* 64-bit first: on a 32-bit system, tv_sec * 1000 would overflow after 24 days */
    return (uint32_t)((uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000);
}

bool screen_init(void) {
    lv_init();
    lv_tick_set_cb(tick_ms);

    /* LVGL's fbdev driver only logs an error if it cannot open the screen: check first.
       Usual cause: the user is not in the "video" group (or run with sudo for a first try). */
    if (access(FRAMEBUFFER, R_OK | W_OK) != 0) {
        perror("Cannot open " FRAMEBUFFER);
        return false;
    }

    /* The LVGL display, drawn into the framebuffer: its size is read from /dev/fb0 */
    lv_display_t* disp = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(disp, FRAMEBUFFER);

    printf("Screen %s: %dx%d\n", FRAMEBUFFER,
           (int)lv_display_get_horizontal_resolution(disp),
           (int)lv_display_get_vertical_resolution(disp));
    ui_init();   /* reads the screen size and scales the layout */
    return true;
}

void screen_update(const dashboard_state_t* state) {
    ui_update(state);
}

void screen_tick(void) {
    lv_timer_handler();
}
