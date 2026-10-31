/**
 * ui.c — the dashboard screen (LVGL 9.2, 1600x900), "course" style. Contract: ui.h.
 *
 * Layout (see the "Style course" mockup):
 *   ┌─ UOSM CAR 1 ──────────────────────────────────────── ● MQTT ─┐
 *   │ RUN 4          │          ╭─ 20 ─╮          │ DISTANCE       │
 *   │ 03:12          │       10 │  24  │ 30       │ 1.83 km        │
 *   │ PREVIOUS       │          │ km/h │          ├────────────────┤
 *   │ 3   04:05      │        0 ╰──────╯ 40       │ TIME           │
 *   │ 2 BEST 03:58   │      (speed arc gauge)     │ 14:32:05       │
 *   │ 1   04:21      │                            │                │
 *   ├────────────────┴────────────────────────────┴────────────────┤
 *   │ ◀          [headlight] [running] [wiper]                  ▶  │
 *   └──────────────────────────────────────────────────────────────┘
 *
 * Every size below is for a 1600x900 screen. ui_init() reads the real screen size and scales
 * everything (S()), keeping the proportions: a screen with another shape gets dark bands.
 *
 * ui_init() builds every object once. ui_update() only changes texts, values and colours.
 */
#include "ui.h"

#include "lvgl.h"
#include "ui_assets.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ---------- Sizes and colours ---------- */

#define SCREEN_W     1600  /* design size; the real screen can be any size (see S()) */
#define SCREEN_H     900
#define TOP_H        64    /* top bar */
#define BAR_H        120   /* bottom bar (lights) */
#define LINE_W       2     /* line above the bottom bar */
#define MAIN_H       (SCREEN_H - TOP_H - BAR_H - LINE_W)
#define SIDE_W       420   /* left and right columns; the gauge column takes the rest */
#define CENTER_W     (SCREEN_W - 2 * SIDE_W)
#define MARGIN       32    /* screen edge */
#define GAP          28    /* space around the cards */

#define GAUGE_MAX_KMH 40   /* full arc (to confirm with the team) */
#define GAUGE_R       310  /* radius of the middle of the arc */
#define GAUGE_W       34   /* arc thickness */
#define GAUGE_START   135  /* degrees, 0 = 3 o'clock, clockwise: the arc opens at the bottom */
#define GAUGE_SWEEP   270

#define BLINK_MS     500   /* turn signals: 500 ms on, 500 ms off */
#define RUN_ROWS     3     /* previous runs shown (ui.h) */

#define COLOR_BG_TOP      lv_color_hex(0x121720)
#define COLOR_BG_BOTTOM   lv_color_hex(0x090C10)
#define COLOR_BAR         lv_color_hex(0x0B0E13)
#define COLOR_CARD        lv_color_hex(0x141922)
#define COLOR_LINE        lv_color_hex(0x262D3A)
#define COLOR_TEXT        lv_color_hex(0xF2F5F9)
#define COLOR_TEXT_2      lv_color_hex(0xC9D1DB)
#define COLOR_MUTED       lv_color_hex(0x9AA4B2)
#define COLOR_ACCENT      lv_color_hex(0xFF9F1C)   /* speed, CAR 1, run tab */
#define COLOR_BEST        lv_color_hex(0xFFB21E)
#define COLOR_BEST_BG     lv_color_hex(0x2A2410)
#define COLOR_BLUE        lv_color_hex(0x3BA3FF)   /* distance and time tabs */
#define COLOR_ARC_BG      lv_color_hex(0x1C232E)
#define COLOR_TICK_MAJOR  lv_color_hex(0x5B6575)
#define COLOR_TICK_MINOR  lv_color_hex(0x3A4250)
#define COLOR_TICK_TEXT   lv_color_hex(0x6B7584)
#define COLOR_ICON_ON     lv_color_hex(0x3DDC84)
#define COLOR_ICON_OFF    lv_color_hex(0x4A5361)
#define COLOR_TILE_ON     lv_color_hex(0x12261A)
#define COLOR_TILE_OFF    lv_color_hex(0x171C24)
#define COLOR_OK_BG       lv_color_hex(0x12301F)
#define COLOR_OK_TEXT     lv_color_hex(0x4ADE80)
#define COLOR_BAD_BG      lv_color_hex(0x3A1616)
#define COLOR_BAD_TEXT    lv_color_hex(0xFF7A7A)

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

typedef struct {
    lv_obj_t* number;   /* run number, e.g. "3" */
    lv_obj_t* best;     /* "BEST" badge, hidden unless this run is the best */
    lv_obj_t* time;
    int shown_best;
} run_row_t;

static lv_obj_t* s_speed;
static lv_obj_t* s_gauge;
static lv_obj_t* s_run_title;
static lv_obj_t* s_run_time;
static run_row_t s_runs[RUN_ROWS];
static lv_obj_t* s_distance;
static lv_obj_t* s_clock;

static indicator_t s_left, s_right, s_headlight, s_running, s_wiper;
static status_pill_t s_mqtt;

/* ---------- Scale: the layout is drawn for 1600x900 and scaled to the real screen ---------- */

static double s_scale = 1.0;   /* real screen / design, set once by ui_init() */

/* A length of the 1600x900 design, in pixels of the real screen (never rounded down to 0). */
static int32_t S(double px) {
    const int32_t v = (int32_t)lround(px * s_scale);
    if (px > 0 && v < 1) return 1;
    if (px < 0 && v > -1) return -1;
    return v;
}

/* ---------- Fonts: two TTF files (Exo 2 italic), drawn at any size ---------- */

static const lv_font_t* font_label(int32_t px) {      /* SemiBold: labels, units */
    return lv_tiny_ttf_create_data(ui_font_exo2_semibold, ui_font_exo2_semibold_size, S(px));
}

static const lv_font_t* font_number(int32_t px) {     /* ExtraBold: numbers */
    return lv_tiny_ttf_create_data(ui_font_exo2_extrabold, ui_font_exo2_extrabold_size, S(px));
}

/* ---------- Small builders ---------- */

/* An empty rectangle with no theme style (no border, no padding, not scrollable). Design units. */
static lv_obj_t* box(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, S(x), S(y));
    lv_obj_set_size(o, S(w), S(h));
    return o;
}

static void fill(lv_obj_t* o, lv_color_t color) {
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
}

static lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* f, lv_color_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

/* Small grey spaced-out title ("DISTANCE", "PREVIOUS"...). */
static lv_obj_t* title(lv_obj_t* parent, const char* text, const lv_font_t* f) {
    lv_obj_t* l = label(parent, text, f, COLOR_MUTED);
    lv_obj_set_style_text_letter_space(l, S(4), 0);
    return l;
}

/* A dark card with a coloured tab in its top-left corner. Children are stacked top to bottom. */
static lv_obj_t* card(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h,
                      int32_t pad_top, lv_color_t tab_color) {
    lv_obj_t* c = box(parent, x, y, w, h);
    fill(c, COLOR_CARD);
    lv_obj_set_style_border_color(c, COLOR_LINE, 0);
    lv_obj_set_style_border_width(c, S(1), 0);
    lv_obj_set_style_radius(c, S(10), 0);
    lv_obj_set_style_clip_corner(c, true, 0);         /* the tab follows the rounded corner */
    lv_obj_set_style_pad_hor(c, S(28), 0);
    lv_obj_set_style_pad_top(c, S(pad_top), 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* tab = box(c, 0, 0, 120, 6);
    fill(tab, tab_color);
    lv_obj_add_flag(tab, LV_OBJ_FLAG_IGNORE_LAYOUT);  /* placed by hand, not by the flex column */
    lv_obj_align(tab, LV_ALIGN_TOP_LEFT, S(-28), S(-pad_top));   /* cancel the card's padding */
    return c;
}

/* A transparent row; children are aligned on their bottom edge. */
static lv_obj_t* row(lv_obj_t* parent, int32_t gap) {
    lv_obj_t* r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(r, S(gap), 0);
    return r;
}

/* A value followed by its unit, both sitting on the same baseline ("1.83" + "km"). */
static lv_obj_t* value_with_unit(lv_obj_t* parent, const lv_font_t* value_font,
                                 const char* unit, const lv_font_t* unit_font) {
    lv_obj_t* r = row(parent, 12);
    lv_obj_t* value = label(r, NO_VALUE, value_font, COLOR_TEXT);
    lv_obj_t* u = label(r, unit, unit_font, COLOR_MUTED);
    /* Bottom-aligned boxes do not share a baseline: lift the unit by the difference of descents. */
    lv_obj_set_style_pad_bottom(u, value_font->base_line - unit_font->base_line, 0);
    return value;
}

static indicator_t indicator(lv_obj_t* parent, const lv_image_dsc_t* image) {
    indicator_t ind;
    ind.shown = -1;
    ind.tile = lv_obj_create(parent);
    lv_obj_remove_style_all(ind.tile);
    lv_obj_set_size(ind.tile, S(78), S(78));
    lv_obj_set_style_radius(ind.tile, S(12), 0);
    lv_obj_set_style_bg_opa(ind.tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ind.tile, COLOR_ICON_ON, 0);

    ind.icon = lv_image_create(ind.tile);
    lv_image_set_src(ind.icon, image);
    lv_image_set_scale(ind.icon, (uint32_t)lround(256 * s_scale));   /* 256 = 100 % */
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
    lv_obj_set_style_radius(p.box, S(8), 0);
    lv_obj_set_style_bg_opa(p.box, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(p.box, S(14), 0);
    lv_obj_set_style_pad_ver(p.box, S(6), 0);
    lv_obj_set_flex_flow(p.box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p.box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(p.box, S(8), 0);

    lv_obj_t* dot = lv_obj_create(p.box);           /* coloured like the text: see set_pill() */
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, S(12), S(12));
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

    p.text = label(p.box, "", f, COLOR_TEXT);
    lv_obj_set_style_text_letter_space(p.text, S(2), 0);
    return p;
}

/* ---------- The speed gauge ---------- */

/* Point at angle deg (0 = 3 o'clock, clockwise) and radius r around (cx, cy):
   design units in, real screen pixels out. */
static lv_point_precise_t polar(int32_t cx, int32_t cy, double r, double deg) {
    const double a = deg * 3.14159265358979323846 / 180.0;   /* M_PI is not standard C */
    lv_point_precise_t p = {(lv_value_precise_t)((cx + r * cos(a)) * s_scale),
                           (lv_value_precise_t)((cy + r * sin(a)) * s_scale)};
    return p;
}

/* Arc 0..GAUGE_MAX_KMH, ticks every 5 km/h, numbers every 10. Returns the arc. */
static lv_obj_t* speed_gauge(lv_obj_t* parent, int32_t cx, int32_t cy, const lv_font_t* tick_font) {
    /* The arc: grey track (MAIN part) + orange value (INDICATOR part). LVGL draws it inside the
       object, from the outer edge inwards, so the object's half-size is the outer radius. */
    const int32_t outer = GAUGE_R + GAUGE_W / 2;
    lv_obj_t* arc = lv_arc_create(parent);
    lv_obj_remove_style_all(arc);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);   /* display only, no knob */
    lv_obj_set_size(arc, 2 * S(outer), 2 * S(outer));
    lv_obj_set_pos(arc, S(cx) - S(outer), S(cy) - S(outer));
    lv_arc_set_bg_angles(arc, GAUGE_START, GAUGE_START + GAUGE_SWEEP);
    lv_arc_set_range(arc, 0, GAUGE_MAX_KMH);
    lv_arc_set_value(arc, 0);
    lv_obj_set_style_arc_width(arc, S(GAUGE_W), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, COLOR_ARC_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, S(GAUGE_W), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);

    /* Ticks inside the arc: long ones with a number every 10 km/h, short ones in between.
       lv_line keeps a pointer to its points: they must live as long as the screen (static). */
    static lv_point_precise_t ticks[9][2];
    for (int k = 0; k <= 8; k++) {
        const bool major = (k % 2 == 0);
        const double deg = GAUGE_START + k * (GAUGE_SWEEP / 8.0);
        ticks[k][0] = polar(cx, cy, major ? 262 : 274, deg);
        ticks[k][1] = polar(cx, cy, 288, deg);
        lv_obj_t* line = lv_line_create(parent);
        lv_line_set_points(line, ticks[k], 2);
        lv_obj_set_style_line_width(line, S(major ? 5 : 3), 0);
        lv_obj_set_style_line_color(line, major ? COLOR_TICK_MAJOR : COLOR_TICK_MINOR, 0);
        lv_obj_set_style_line_rounded(line, true, 0);

        if (major) {
            char text[8];
            snprintf(text, sizeof text, "%d", k * GAUGE_MAX_KMH / 8);
            lv_obj_t* l = label(parent, text, tick_font, COLOR_TICK_TEXT);
            const lv_point_precise_t c = polar(cx, cy, 228, deg);
            lv_obj_set_pos(l, (int32_t)c.x, (int32_t)c.y);
            lv_obj_set_style_translate_x(l, LV_PCT(-50), 0);   /* centre the text on the point */
            lv_obj_set_style_translate_y(l, LV_PCT(-50), 0);
        }
    }
    return arc;
}

/* ---------- ui_init ---------- */

void ui_init(void) {
    /* First the scale: every size below (fonts included) goes through S() */
    lv_display_t* disp = lv_display_get_default();
    const double sx = (double)lv_display_get_horizontal_resolution(disp) / SCREEN_W;
    const double sy = (double)lv_display_get_vertical_resolution(disp) / SCREEN_H;
    s_scale = sx < sy ? sx : sy;

    const lv_font_t* f_label     = font_label(26);
    const lv_font_t* f_label_sm  = font_label(20);
    const lv_font_t* f_tick      = font_label(28);
    const lv_font_t* f_pill      = font_label(22);
    const lv_font_t* f_badge     = font_label(18);
    const lv_font_t* f_unit      = font_label(34);
    const lv_font_t* f_speed_u   = font_label(44);
    const lv_font_t* f_brand     = font_number(30);
    const lv_font_t* f_speed     = font_number(260);
    const lv_font_t* f_run       = font_number(124);
    const lv_font_t* f_prev      = font_number(52);
    const lv_font_t* f_distance  = font_number(112);
    const lv_font_t* f_clock     = font_number(80);

    lv_obj_t* scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, COLOR_BG_TOP, 0);
    lv_obj_set_style_bg_grad_color(scr, COLOR_BG_BOTTOM, 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* The design (1600x900) is drawn in a "root" box of the same proportions, centred */
    lv_obj_t* root = lv_obj_create(scr);
    lv_obj_remove_style_all(root);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(root, S(SCREEN_W), S(SCREEN_H));
    lv_obj_center(root);

    /* Top bar: team name on the left, MQTT status on the right */
    lv_obj_t* top = box(root, 0, 0, SCREEN_W, TOP_H);
    fill(top, COLOR_BAR);
    lv_obj_set_style_border_side(top, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(top, COLOR_LINE, 0);
    lv_obj_set_style_border_width(top, S(1), 0);
    lv_obj_set_style_pad_hor(top, S(MARGIN), 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* brand = row(top, 12);
    lv_obj_set_style_text_letter_space(label(brand, "UOSM", f_brand, COLOR_TEXT), S(4), 0);
    lv_obj_set_style_text_letter_space(label(brand, "CAR 1", f_brand, COLOR_ACCENT), S(4), 0);
    s_mqtt = status_pill(top, f_pill);

    /* Left column: current run, then the previous ones */
    lv_obj_t* c = card(root, MARGIN, TOP_H + GAP, SIDE_W - MARGIN, MAIN_H - 2 * GAP, 32, COLOR_ACCENT);
    s_run_title = title(c, "RUN", f_label);
    s_run_time = label(c, NO_VALUE, f_run, COLOR_TEXT);
    lv_obj_set_style_margin_bottom(s_run_time, S(22), 0);
    lv_obj_set_style_margin_bottom(title(c, "PREVIOUS", f_label_sm), S(6), 0);
    for (int k = 0; k < RUN_ROWS; k++) {
        lv_obj_t* r = lv_obj_create(c);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_ver(r, S(10), 0);
        lv_obj_set_style_border_side(r, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_color(r, COLOR_LINE, 0);
        lv_obj_set_style_border_width(r, S(1), 0);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* left = row(r, 10);
        lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        s_runs[k].number = label(left, "", f_label, COLOR_MUTED);
        s_runs[k].best = label(left, "BEST", f_badge, COLOR_BEST);
        fill(s_runs[k].best, COLOR_BEST_BG);
        lv_obj_set_style_radius(s_runs[k].best, S(6), 0);
        lv_obj_set_style_pad_hor(s_runs[k].best, S(10), 0);
        lv_obj_set_style_pad_ver(s_runs[k].best, S(2), 0);
        s_runs[k].time = label(r, NO_VALUE, f_prev, COLOR_TEXT_2);
        s_runs[k].shown_best = -1;
    }

    /* Centre: the speed gauge, with the number in the middle */
    lv_obj_t* centre = box(root, SIDE_W, TOP_H, CENTER_W, MAIN_H);
    const int32_t gx = CENTER_W / 2, gy = MAIN_H / 2;
    s_gauge = speed_gauge(centre, gx, gy, f_tick);
    lv_obj_t* speed = lv_obj_create(centre);
    lv_obj_remove_style_all(speed);
    lv_obj_set_size(speed, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(speed, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(speed, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(speed, LV_ALIGN_CENTER, 0, 0);
    title(speed, "SPEED", f_tick);
    s_speed = label(speed, NO_VALUE, f_speed, COLOR_ACCENT);
    lv_obj_set_style_margin_ver(s_speed, S(-24), 0);      /* the font's empty space above and below the digits */
    label(speed, "km/h", f_speed_u, COLOR_MUTED);

    /* Right column: distance, time */
    const int32_t rx = SIDE_W + CENTER_W;
    const int32_t rw = SIDE_W - MARGIN;
    const int32_t rh = (MAIN_H - 3 * GAP) / 2;
    c = card(root, rx, TOP_H + GAP, rw, rh, 0, COLOR_BLUE);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    title(c, "DISTANCE", f_label);
    s_distance = value_with_unit(c, f_distance, "km", f_unit);

    c = card(root, rx, TOP_H + 2 * GAP + rh, rw, rh, 0, COLOR_BLUE);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    title(c, "TIME", f_label);
    s_clock = label(c, NO_VALUE, f_clock, COLOR_TEXT);

    /* Line above the bottom bar: grey at the ends, orange in the middle */
    lv_obj_t* l = box(root, 0, TOP_H + MAIN_H, SCREEN_W / 2, LINE_W);
    lv_obj_set_style_bg_color(l, COLOR_LINE, 0);
    lv_obj_set_style_bg_grad_color(l, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_grad_dir(l, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    l = box(root, SCREEN_W / 2, TOP_H + MAIN_H, SCREEN_W / 2, LINE_W);
    lv_obj_set_style_bg_color(l, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_grad_color(l, COLOR_LINE, 0);
    lv_obj_set_style_bg_grad_dir(l, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);

    /* Bottom bar: turn signals at both ends, like in a car; the other lights in the middle */
    lv_obj_t* bar = box(root, 0, SCREEN_H - BAR_H, SCREEN_W, BAR_H);
    fill(bar, COLOR_BAR);
    lv_obj_set_style_pad_hor(bar, S(MARGIN), 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_left = indicator(bar, &ui_icon_left);
    lv_obj_t* lights = row(bar, 14);
    s_headlight = indicator(lights, &ui_icon_headlight);
    s_running   = indicator(lights, &ui_icon_running);
    s_wiper     = indicator(lights, &ui_icon_wiper);
    s_right = indicator(bar, &ui_icon_right);
}

/* ---------- ui_update helpers ---------- */

/* Changing a label's text makes LVGL redraw it: skip it when nothing changed (every 100 ms). */
static void set_text(lv_obj_t* l, const char* text) {
    if (strcmp(lv_label_get_text(l), text) != 0) {
        lv_label_set_text(l, text);
    }
}

/* "MM:SS", or "H:MM:SS" from one hour. */
static void format_duration(char* buf, size_t size, double seconds) {
    const long s = (long)seconds;
    if (s < 3600) {
        snprintf(buf, size, "%02ld:%02ld", s / 60, s % 60);
    } else {
        snprintf(buf, size, "%ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60);
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
    lv_obj_set_style_border_width(ind->tile, on ? S(2) : 0, 0);
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

/* Previous runs: number, time, and the BEST badge on the shortest one. */
static void update_runs(const dashboard_state_t* st) {
    int best = -1;
    for (int k = 0; k < RUN_ROWS; k++) {
        const ui_value_t* r = &st->previous_runs_s[k];
        if (r->valid && (best < 0 || r->value < st->previous_runs_s[best].value)) {
            best = k;
        }
    }

    char buf[16];
    for (int k = 0; k < RUN_ROWS; k++) {
        run_row_t* row_k = &s_runs[k];
        const ui_value_t* r = &st->previous_runs_s[k];
        const int number = st->run_number - 1 - k;   /* [0] = the run just before the current one */
        if (r->valid && number > 0) {
            snprintf(buf, sizeof buf, "%d", number);
            set_text(row_k->number, buf);
            format_duration(buf, sizeof buf, r->value);
            set_text(row_k->time, buf);
        } else {
            set_text(row_k->number, "");
            set_text(row_k->time, NO_VALUE);
        }

        const int is_best = (k == best);
        if (is_best != row_k->shown_best) {
            row_k->shown_best = is_best;
            if (is_best) {
                lv_obj_remove_flag(row_k->best, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(row_k->best, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_set_style_text_color(row_k->time, is_best ? COLOR_BEST : COLOR_TEXT_2, 0);
        }
    }
}

/* ---------- ui_update ---------- */

void ui_update(const dashboard_state_t* st) {
    char buf[32];

    /* Speed: the number (orange, grey dashes when unknown) and the arc */
    static int speed_shown_valid = -1;
    if ((int)st->speed_kmh.valid != speed_shown_valid) {
        speed_shown_valid = st->speed_kmh.valid;
        lv_obj_set_style_text_color(s_speed, st->speed_kmh.valid ? COLOR_ACCENT : COLOR_MUTED, 0);
    }
    if (st->speed_kmh.valid) {
        snprintf(buf, sizeof buf, "%.0f", st->speed_kmh.value);
        set_text(s_speed, buf);
        double v = round(st->speed_kmh.value);
        v = v < 0 ? 0 : (v > GAUGE_MAX_KMH ? GAUGE_MAX_KMH : v);
        if (lv_arc_get_value(s_gauge) != (int32_t)v) {
            lv_arc_set_value(s_gauge, (int32_t)v);
        }
    } else {
        set_text(s_speed, NO_VALUE);
        if (lv_arc_get_value(s_gauge) != 0) {
            lv_arc_set_value(s_gauge, 0);
        }
    }

    if (st->distance_m.valid) {
        snprintf(buf, sizeof buf, "%.2f", st->distance_m.value / 1000.0);
        set_text(s_distance, buf);
    } else {
        set_text(s_distance, NO_VALUE);
    }

    /* Current run */
    if (st->run_number > 0) {
        snprintf(buf, sizeof buf, "RUN %d", st->run_number);
        set_text(s_run_title, buf);
    } else {
        set_text(s_run_title, "RUN");
    }
    if (st->run_time_s.valid && st->run_time_s.value >= 0) {
        format_duration(buf, sizeof buf, st->run_time_s.value);
        set_text(s_run_time, buf);
    } else {
        set_text(s_run_time, NO_VALUE);
    }
    update_runs(st);

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
}
