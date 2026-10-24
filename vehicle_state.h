#ifndef VEHICLE_STATE_H
#define VEHICLE_STATE_H

#include "mqtt_decode.h" /* telemetry_t */

/* One time value per telemetry field, same names as telemetry_t.
 * Inside vehicle_state.c: when the field was last updated (seconds).
 * In a snapshot:          how old the field is (seconds). */
typedef struct {
    double ts, fix, lat, lon, alt, gs, ax, ay, az;
    double spd, dist, v, i, e, thr, ap, l1, l2, lts;
} vehicle_times_t;

/* Forget everything: every has_ flag back to false ("never received"). */
void vehicle_state_init(void);

/* Merge a decoded message: only fields with has_ == true are overwritten,
 * and only those fields get their timestamp set to now_s. */
void vehicle_state_update(const telemetry_t* msg, double now_s);

/* Copy the current state out.
 * data->has_x == false means x was never received: ignore ages->x then.
 * ages->x is how many seconds ago x was last updated. */
void vehicle_state_snapshot(telemetry_t* data, vehicle_times_t* ages, double now_s);

#endif /* VEHICLE_STATE_H */