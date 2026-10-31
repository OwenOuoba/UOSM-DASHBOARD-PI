/**
 * dashboard_state.h — turns the vehicle state into what the screen shows.
 *
 * The one rule for every value: valid = received AND not older than STALE_S
 * (vehicle_state.h). A value that is not valid is shown as "--" (ui.h).
 */
#ifndef DASHBOARD_STATE_H
#define DASHBOARD_STATE_H

#include "ui.h" /* dashboard_state_t */

#include <stdbool.h>

/* Fills *out from vehicle_state (snapshot at now_s). Call every 100 ms. */
void dashboard_state_build(dashboard_state_t* out, double now_s, bool mqtt_connected);

#endif /* DASHBOARD_STATE_H */
