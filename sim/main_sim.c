/**
 * main_sim.c — the dashboard screen on a PC, in a 1600x900 SDL window (same size as the Pi's HDMI screen).
 * No MQTT here: a fake dashboard_state_t moves so the UI can be developed and checked without the car.
 *
 *   ui_sim                        window with the demo (every 40 s: 30 s normal, then 10 s "MQTT lost")
 *   ui_sim --shot normal lost     no window: saves both screens as normal.ppm and lost.ppm
 *   --size WxH (last argument)    another screen size, e.g. --size 1024x600 (default 1600x900)
 */
#include "lvgl.h"
#include "ui.h"

#include <SDL2/SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Screen size: the Pi's HDMI screen by default, or --size WxH */
static int32_t s_screen_w = 1600;
static int32_t s_screen_h = 900;

#define UPDATE_MS 100      /* ui_update() period, as on the Pi */
#define CYCLE_S   40       /* demo cycle */
#define LOST_S    30       /* from this second of the cycle, MQTT is "lost" */

static const ui_value_t NONE = {false, 0};

static ui_value_t val(double v) {
    ui_value_t u = {true, v};
    return u;
}

/* Fake runs: each demo run lasts RUN_DEMO_S; finished runs get times between 03:50 and 04:30. */
#define RUN_DEMO_S 25

static double fake_run_time(int run) {
    return 230 + (run * 37) % 41;
}

/* Fake car: what the core will build from MQTT data on the Pi. t = seconds since start. */
static dashboard_state_t demo_state(double t, double distance_m) {
    dashboard_state_t s;
    memset(&s, 0, sizeof s);
    const double cycle = fmod(t, CYCLE_S);
    const bool mqtt_ok = cycle < LOST_S;

    s.mqtt_connected = mqtt_ok;
    s.clock_ms = val((double)time(NULL) * 1000.0);       /* the PC clock, in UTC */

    /* Runs: a new run starts every RUN_DEMO_S (the core will do it on the button) */
    s.run_number = 4 + (int)(t / RUN_DEMO_S);
    s.run_time_s = val(180 + fmod(t, RUN_DEMO_S));
    for (int k = 0; k < 3; k++) {
        const int run = s.run_number - 1 - k;
        s.previous_runs_s[k] = run > 0 ? val(fake_run_time(run)) : NONE;
    }

    /* Values coming from MQTT disappear when it is lost */
    s.speed_kmh = mqtt_ok ? val(25 + 8 * sin(t / 4)) : NONE;
    s.distance_m = mqtt_ok ? val(distance_m) : NONE;
    s.lights_valid = mqtt_ok;
    s.headlight = true;
    s.running_lights = true;
    s.left_turn = fmod(t, 20) < 6;
    s.right_turn = fmod(t, 20) >= 10 && fmod(t, 20) < 14;
    s.wiper = false;
    return s;
}

/* Saves the current screen as a PPM image (simple format any image viewer or Python can open). */
static int save_screen(const char* path) {
    lv_draw_buf_t* img = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_XRGB8888);
    if (img == NULL) return 1;
    FILE* f = fopen(path, "wb");
    if (f == NULL) { lv_draw_buf_destroy(img); return 1; }
    fprintf(f, "P6\n%d %d\n255\n", (int)img->header.w, (int)img->header.h);
    for (uint32_t y = 0; y < img->header.h; y++) {
        const uint8_t* row = img->data + y * img->header.stride;
        for (uint32_t x = 0; x < img->header.w; x++) {
            const uint8_t rgb[3] = {row[4 * x + 2], row[4 * x + 1], row[4 * x]};   /* stored B, G, R, X */
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    lv_draw_buf_destroy(img);
    printf("saved %s\n", path);
    return 0;
}

static void no_flush(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
    (void)a; (void)px;
    lv_display_flush_ready(d);
}

static int shot_mode(const char* normal_path, const char* lost_path) {
    static uint8_t buf[4096 * 100 * 4];                   /* a display without a window, up to 4096 px wide */
    lv_display_t* d = lv_display_create(s_screen_w, s_screen_h);
    lv_display_set_flush_cb(d, no_flush);
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    ui_init();

    /* Same values as the mockup: run 4 at 03:12, previous runs 04:05, 03:58 (best), 04:21 */
    dashboard_state_t s = demo_state(2.0, 1830);          /* left turn on, lights valid */
    s.speed_kmh = val(24);
    s.run_number = 4;
    s.run_time_s = val(192);
    s.previous_runs_s[0] = val(245);
    s.previous_runs_s[1] = val(238);
    s.previous_runs_s[2] = val(261);
    ui_update(&s);
    int err = save_screen(normal_path);

    dashboard_state_t lost = demo_state(LOST_S + 2.0, 0);
    lost.run_number = s.run_number;
    lost.run_time_s = s.run_time_s;
    memcpy(lost.previous_runs_s, s.previous_runs_s, sizeof lost.previous_runs_s);
    ui_update(&lost);
    err |= save_screen(lost_path);
    return err;
}

int main(int argc, char** argv) {
    lv_init();
    lv_tick_set_cb(SDL_GetTicks);   /* LVGL needs to know the time in ms */

    if (argc >= 3 && strcmp(argv[argc - 2], "--size") == 0) {
        int w, h;
        if (sscanf(argv[argc - 1], "%dx%d", &w, &h) == 2 && w >= 100 && w <= 4096 && h >= 100 && h <= 4096) {
            s_screen_w = w;
            s_screen_h = h;
        }
        argc -= 2;
    }

    if (argc == 4 && strcmp(argv[1], "--shot") == 0) {
        return shot_mode(argv[2], argv[3]);
    }

    lv_sdl_window_create(s_screen_w, s_screen_h);
    ui_init();

    const uint32_t start = SDL_GetTicks();
    uint32_t last_update = 0;
    double distance_m = 0;

    while (1) {
        const uint32_t now = SDL_GetTicks();
        if (now - last_update >= UPDATE_MS) {
            const double t = (now - start) / 1000.0;
            const dashboard_state_t s = demo_state(t, distance_m);
            if (s.speed_kmh.valid) distance_m += s.speed_kmh.value / 3.6 * UPDATE_MS / 1000.0;
            ui_update(&s);
            last_update = now;
        }
        const uint32_t wait_ms = lv_timer_handler();   /* draws what changed, returns when to come back */
        SDL_Delay(wait_ms < 5 ? 5 : wait_ms);
    }
}
