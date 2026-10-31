/**
 * ui.h — the contract between the dashboard core and the screen (LVGL).
 *
 * The core (MQTT, vehicle_state, button) decides WHAT is true.
 * The UI only decides HOW to show it: it never talks to MQTT, never reads
 * JSON, never handles the button. This is why the UI can be developed and
 * tested on a PC with fake values, without the car.
 *
 * Rules:
 *  1. Every value comes with a valid flag. valid == false -> the screen shows "--".
 *     The core sets valid = false when the field was never received or is too old
 *     (older than STALE_S, defined in vehicle_state.h).
 *  2. The core fills a dashboard_state_t and calls ui_update() every 100 ms.
 *     The UI copies what it needs and never keeps the pointer.
 *  3. ui_init() and ui_update() touch LVGL: call them ONLY from the thread that
 *     runs lv_timer_handler(), never from an MQTT callback running elsewhere.
 *  4. Adding something to the screen means changing this file: agree on it first.
 */
#ifndef UI_H
#define UI_H

#include <stdbool.h>

/* One value to display, and whether it can be trusted. */
typedef struct {
    bool   valid;  /* false -> "--" on screen */
    double value;
} ui_value_t;

typedef struct {
    ui_value_t speed_kmh;        /* spd, Hall-effect speedometer                          */
    ui_value_t distance_m;       /* dist                                                  */
    ui_value_t clock_ms;         /* current time, UTC epoch in ms (GPS ts);
                                    the UI shows it in Ottawa local time (EST/EDT)        */
    ui_value_t run_time_s;       /* time since the run button was pressed (physical button,
                                    handled by the core)                                   */
    int        run_number;       /* current run, counted from 1; 0 = no run started yet    */
    ui_value_t previous_runs_s[3]; /* finished runs, [0] = the last one, then older;
                                    valid == false -> no such run. The UI marks the best  */

    bool lights_valid;           /* false -> indicators shown as unknown (grey), not off   */
    bool headlight;              /* from lts, characters 1 to 5                            */
    bool running_lights;
    bool left_turn;
    bool right_turn;
    bool wiper;

    bool mqtt_connected;         /* connected to the broker                                */
} dashboard_state_t;

/** Builds the screen. Call once at startup, after LVGL and the display are initialised. */
void ui_init(void);

/** Refreshes the screen from a snapshot of the state. Call every 100 ms. */
void ui_update(const dashboard_state_t* state);

#endif /* UI_H */
