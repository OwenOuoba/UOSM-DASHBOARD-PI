/**
 * ui.c — the dashboard screen (LVGL 9.2, 1600x900). Contract: ui.h.
 *
 * Layout (see the "B on light background" mockup):
 *   ┌──────────────────────┬──────────────┐
 *   │ SPEED                │ RUN TIME     │
 *   │ 24 km/h  (huge)      ├──────────────┤
 *   │                      │ DISTANCE     │
 *   │                      ├──────────────┤
 *   │                      │ TIME         │
 *   ├──────────────────────┴──────────────┤
 *   │ ◀ [head][run][wiper] ▶   MQTT   NET │
 *   └─────────────────────────────────────┘
 *
 * ui_init() builds every object once. ui_update() only changes texts and colours.
 */
#include "ui.h"

#include "lvgl.h"
#include "ui_assets.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ---------- Sizes and colours ---------- */

#define SCREEN_W     1600
#define SCREEN_H     900
#define LEFT_W       900   /* speed column */
#define BAR_H        120   /* bottom bar */
#define LINE_W       2     /* divider lines */
#define TOP_H        (SCREEN_H - BAR_H - LINE_W)
#define RUN_H        320   /* right column: run time row; distance and time share the rest */

#define BLINK_MS     500   /* turn signals: 500 ms on, 500 ms off */

#define COLOR_BG          lv_color_hex(0xF3F4F1)
#define COLOR_TEXT        lv_color_hex(0x121417)
#define COLOR_MUTED       lv_color_hex(0x4E555B)
#define COLOR_LINE        lv_color_hex(0xD3D7CF)
#define COLOR_ICON_ON     lv_color_hex(0x1E8E3E)
#define COLOR_ICON_OFF    lv_color_hex(0xB3B8B0)
#define COLOR_TILE_ON     lv_color_hex(0xFFFFFF)
#define COLOR_TILE_OFF    lv_color_hex(0xE4E6E1)
#define COLOR_OK_BG       lv_color_hex(0xDCEFE3)
#define COLOR_OK_TEXT     lv_color_hex(0x1E6B3A)
#define COLOR_BAD_BG      lv_color_hex(0xF7DEDA)
#define COLOR_BAD_TEXT    lv_color_hex(0xA3271A)

#define NO_VALUE "--"

/* ---------- Objects kept for ui_update() ---------- */

typedef struct {
    lv_obj_t* tile;
    lv_obj_t* icon;
    int shown;          /* look currently on screen, -1 = none yet (restyle only on change) */
} indicator_t;

typedef struct {
    lv_obj_t* box;
    lv_obj_t* text;
    int shown;
} status_pill_t;

static lv_obj_t* s_speed;
static lv_obj_t* s_run_time;
static lv_obj_t* s_distance;
static lv_obj_t* s_clock;

static indicator_t s_left, s_right, s_headlight, s_running, s_wiper;
static status_pill_t s_mqtt, s_net;

/* ---------- Fonts: one TTF file, drawn at several sizes ---------- */

static const lv_font_t* font(int32_t px) {
    return lv_tiny_ttf_create_data(ui_font_barlow_condensed, ui_font_barlow_condensed_size, px);
}

/* ---------- Small builders ---------- */

/* An empty rectangle with no theme style (no border, no padding, not scrollable). */
static lv_obj_t* box(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    return o;
}

static void divider(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h) {
    lv_obj_t* o = box(parent, x, y, w, h);
    lv_obj_set_style_bg_color(o, COLOR_LINE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
}

static lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* f, lv_color_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

/* Small grey title above a value ("SPEED", "DISTANCE"...). */
static lv_obj_t* title(lv_obj_t* parent, const char* text, const lv_font_t* f) {
    lv_obj_t* l = label(parent, text, f, COLOR_MUTED);
    lv_obj_set_style_text_letter_space(l, 3, 0);
    return l;
}

/* A transparent column: children stacked top to bottom, vertically centred in the cell. */
static lv_obj_t* cell(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, int32_t pad_left) {
    lv_obj_t* c = box(parent, x, y, w, h);
    lv_obj_set_style_pad_left(c, pad_left, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    return c;
}

/* A value followed by its unit, both sitting on the same baseline ("24" + "km/h"). */
static lv_obj_t* value_with_unit(lv_obj_t* parent, const lv_font_t* value_font,
                                 const char* unit, const lv_font_t* unit_font, lv_obj_t** value_out) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 16, 0);

    *value_out = label(row, NO_VALUE, value_font, COLOR_TEXT);
    lv_obj_t* u = label(row, unit, unit_font, COLOR_MUTED);
    /* Bottom-aligned boxes do not share a baseline: lift the unit by the difference of descents. */
    lv_obj_set_style_pad_bottom(u, value_font->base_line - unit_font->base_line, 0);
    return row;
}

static indicator_t indicator(lv_obj_t* parent, const lv_image_dsc_t* image) {
    indicator_t ind;
    ind.shown = -1;
    ind.tile = lv_obj_create(parent);
    lv_obj_remove_style_all(ind.tile);
    lv_obj_set_size(ind.tile, 78, 78);
    lv_obj_set_style_radius(ind.tile, 14, 0);
    lv_obj_set_style_bg_opa(ind.tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ind.tile, COLOR_ICON_ON, 0);

    ind.icon = lv_image_create(ind.tile);
    lv_image_set_src(ind.icon, image);
    lv_obj_set_style_image_recolor_opa(ind.icon, LV_OPA_COVER, 0);
    lv_obj_center(ind.icon);
    return ind;
}

static status_pill_t status_pill(lv_obj_t* parent, const lv_font_t* f) {
    status_pill_t p;
    p.shown = -1;
    p.box = lv_obj_create(parent);
    lv_obj_remove_style_all(p.box);
    lv_obj_set_size(p.box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(p.box, 12, 0);
    lv_obj_set_style_bg_opa(p.box, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(p.box, 20, 0);
    lv_obj_set_style_pad_ver(p.box, 10, 0);
    lv_obj_set_flex_flow(p.box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p.box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(p.box, 12, 0);

    lv_obj_t* dot = lv_obj_create(p.box);           /* coloured like the text: see set_pill() */
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 16, 16);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

    p.text = label(p.box, "", f, COLOR_TEXT);
    return p;
}

/* ---------- ui_init ---------- */

void ui_init(void) {
    const lv_font_t* f_title_big = font(44);
    const lv_font_t* f_title     = font(34);
    const lv_font_t* f_speed     = font(480);
    const lv_font_t* f_speed_u   = font(72);
    const lv_font_t* f_run       = font(130);
    const lv_font_t* f_distance  = font(120);
    const lv_font_t* f_clock     = font(110);
    const lv_font_t* f_unit      = font(36);
    const lv_font_t* f_pill      = font(30);

    lv_obj_t* scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Speed, left column */
    lv_obj_t* c = cell(scr, 0, 0, LEFT_W, TOP_H, 60);
    title(c, "SPEED", f_title_big);
    lv_obj_t* speed_row = value_with_unit(c, f_speed, "km/h", f_speed_u, &s_speed);
    lv_obj_set_style_margin_top(speed_row, -80, 0);   /* the font's empty space above the digits */
    divider(scr, LEFT_W, 0, LINE_W, TOP_H);

    /* Right column: run time, distance, time */
    const int32_t x = LEFT_W + LINE_W;
    const int32_t w = SCREEN_W - x;
    const int32_t small_h = (TOP_H - RUN_H - 2 * LINE_W) / 2;

    c = cell(scr, x, 0, w, RUN_H, 48);
    title(c, "RUN TIME", f_title);
    s_run_time = label(c, NO_VALUE, f_run, COLOR_TEXT);
    divider(scr, x, RUN_H, w, LINE_W);

    c = cell(scr, x, RUN_H + LINE_W, w, small_h, 48);
    title(c, "DISTANCE", f_title);
    value_with_unit(c, f_distance, "km", f_unit, &s_distance);
    divider(scr, x, RUN_H + LINE_W + small_h, w, LINE_W);

    c = cell(scr, x, RUN_H + 2 * LINE_W + small_h, w, small_h, 48);
    title(c, "TIME", f_title);
    s_clock = label(c, NO_VALUE, f_clock, COLOR_TEXT);

    /* Bottom bar: lights on the left, connection status on the right */
    divider(scr, 0, TOP_H, SCREEN_W, LINE_W);
    lv_obj_t* bar = box(scr, 0, TOP_H + LINE_W, SCREEN_W, BAR_H);
    lv_obj_set_style_pad_hor(bar, 40, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* lights = box(bar, 0, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(lights, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(lights, 14, 0);
    s_left      = indicator(lights, &ui_icon_left);
    s_headlight = indicator(lights, &ui_icon_headlight);
    s_running   = indicator(lights, &ui_icon_running);
    s_wiper     = indicator(lights, &ui_icon_wiper);
    s_right     = indicator(lights, &ui_icon_right);
    lv_obj_set_style_margin_left(s_headlight.tile, 16, 0);   /* turn signals stand apart */
    lv_obj_set_style_margin_left(s_right.tile, 16, 0);

    lv_obj_t* status = box(bar, 0, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(status, 14, 0);
    s_mqtt = status_pill(status, f_pill);
    s_net  = status_pill(status, f_pill);
}

/* ---------- ui_update helpers ---------- */

/* Changing a label's text makes LVGL redraw it: skip it when nothing changed (every 100 ms). */
static void set_text(lv_obj_t* l, const char* text) {
    if (strcmp(lv_label_get_text(l), text) != 0) {
        lv_label_set_text(l, text);
    }
}

/* Days since 1970-01-01 for a calendar date (H. Hinnant's days_from_civil). */
static int64_t days_from_civil(int64_t y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* Day of month of the n-th Sunday of a month (n = 1 for the first). */
static int nth_sunday(int64_t year, int month, int n) {
    const int weekday_of_1st = (int)((days_from_civil(year, month, 1) % 7 + 11) % 7);  /* 0 = Sunday */
    return 1 + (7 - weekday_of_1st) % 7 + 7 * (n - 1);
}

/* Ottawa time (America/Toronto) from UTC, without depending on the Pi's time zone setting.
   EDT (UTC-4) from the 2nd Sunday of March 2:00 to the 1st Sunday of November 2:00, else EST (UTC-5). */
static int64_t ottawa_from_utc(int64_t utc_s) {
    const time_t t = (time_t)utc_s;
    const int64_t year = gmtime(&t)->tm_year + 1900;
    const int64_t dst_start = days_from_civil(year, 3, nth_sunday(year, 3, 2)) * 86400 + 7 * 3600;   /* 2:00 EST */
    const int64_t dst_end   = days_from_civil(year, 11, nth_sunday(year, 11, 1)) * 86400 + 6 * 3600; /* 2:00 EDT */
    const bool dst = utc_s >= dst_start && utc_s < dst_end;
    return utc_s + (dst ? -4 : -5) * 3600;
}

static void set_indicator(indicator_t* ind, bool valid, bool on) {
    const int look = valid * 2 + on;
    if (look == ind->shown) return;
    ind->shown = look;
    lv_obj_set_style_bg_color(ind->tile, on ? COLOR_TILE_ON : COLOR_TILE_OFF, 0);
    lv_obj_set_style_border_width(ind->tile, on ? 2 : 0, 0);
    lv_obj_set_style_image_recolor(ind->icon, on ? COLOR_ICON_ON : COLOR_ICON_OFF, 0);
    lv_obj_set_style_opa(ind->tile, valid ? LV_OPA_COVER : LV_OPA_50, 0);   /* unknown: faded */
}

static void set_pill(status_pill_t* p, bool ok, const char* text) {
    set_text(p->text, text);
    if ((int)ok == p->shown) return;
    p->shown = ok;
    const lv_color_t fg = ok ? COLOR_OK_TEXT : COLOR_BAD_TEXT;
    lv_obj_set_style_bg_color(p->box, ok ? COLOR_OK_BG : COLOR_BAD_BG, 0);
    lv_obj_set_style_bg_color(lv_obj_get_child(p->box, 0), fg, 0);   /* the dot */
    lv_obj_set_style_text_color(p->text, fg, 0);
}

/* ---------- ui_update ---------- */

void ui_update(const dashboard_state_t* st) {
    char buf[32];

    if (st->speed_kmh.valid) {
        snprintf(buf, sizeof buf, "%.0f", st->speed_kmh.value);
        set_text(s_speed, buf);
    } else {
        set_text(s_speed, NO_VALUE);
    }

    if (st->distance_m.valid) {
        snprintf(buf, sizeof buf, "%.2f", st->distance_m.value / 1000.0);
        set_text(s_distance, buf);
    } else {
        set_text(s_distance, NO_VALUE);
    }

    if (st->run_time_s.valid && st->run_time_s.value >= 0) {
        const long s = (long)st->run_time_s.value;
        if (s < 3600) {
            snprintf(buf, sizeof buf, "%02ld:%02ld", s / 60, s % 60);
        } else {
            snprintf(buf, sizeof buf, "%ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60);
        }
        set_text(s_run_time, buf);
    } else {
        set_text(s_run_time, NO_VALUE);
    }

    if (st->clock_ms.valid) {
        const int64_t local = ottawa_from_utc((int64_t)(st->clock_ms.value / 1000.0));
        const int64_t sec_of_day = ((local % 86400) + 86400) % 86400;
        snprintf(buf, sizeof buf, "%02d:%02d:%02d",
                 (int)(sec_of_day / 3600), (int)(sec_of_day / 60 % 60), (int)(sec_of_day % 60));
        set_text(s_clock, buf);
    } else {
        set_text(s_clock, NO_VALUE);
    }

    /* Lights. Turn signals blink on screen while they are on, like in a car. */
    const bool v = st->lights_valid;
    const bool blink_phase = (lv_tick_get() / BLINK_MS) % 2 == 0;
    set_indicator(&s_left,      v, v && st->left_turn && blink_phase);
    set_indicator(&s_right,     v, v && st->right_turn && blink_phase);
    set_indicator(&s_headlight, v, v && st->headlight);
    set_indicator(&s_running,   v, v && st->running_lights);
    set_indicator(&s_wiper,     v, v && st->wiper);

    set_pill(&s_mqtt, st->mqtt_connected, "MQTT");
    if (st->network_ping_ms.valid) {
        snprintf(buf, sizeof buf, "NET %.0f ms", st->network_ping_ms.value);
        set_pill(&s_net, true, buf);
    } else {
        set_pill(&s_net, false, "NET " NO_VALUE);
    }
}
