#include "dashboard_state.h"
#include "vehicle_state.h"

#include <string.h>

/* has_ is checked first: the age of a never-received field is meaningless. */
static bool is_fresh(bool has, double age) {
    return has && age <= STALE_S;
}

static ui_value_t make_value(bool has, double value, double age) {
    ui_value_t v;
    v.valid = is_fresh(has, age);
    v.value = v.valid ? value : 0;
    return v;
}

void dashboard_state_build(dashboard_state_t* out, double now_s, bool mqtt_connected) {
    telemetry_t d;
    vehicle_times_t a;
    vehicle_state_snapshot(&d, &a, now_s);

    memset(out, 0, sizeof(*out)); /* every valid flag -> false, every light off */

    out->speed_kmh  = make_value(d.has_spd,  d.spd,  a.spd);
    out->distance_m = make_value(d.has_dist, d.dist, a.dist);

    /* GPS time of the last message + time since it arrived: the clock keeps
     * moving every second between two messages. */
    out->clock_ms = make_value(d.has_ts, (double)d.ts + a.ts * 1000.0, a.ts);

    /* lts: 5 characters, each '0' or '1' (checked by the decoder) */
    out->lights_valid = is_fresh(d.has_lts, a.lts);
    if (out->lights_valid) {
        out->headlight      = (d.lts[0] == '1');
        out->running_lights = (d.lts[1] == '1');
        out->left_turn      = (d.lts[2] == '1');
        out->right_turn     = (d.lts[3] == '1');
        out->wiper          = (d.lts[4] == '1');
    }

    /* run_number, run_time_s, previous_runs_s: 0 and not valid (memset above)
     * until the run button exists. */

    out->mqtt_connected = mqtt_connected;
}
