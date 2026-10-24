#include "vehicle_state.h"

#include <string.h>

/* The state itself. static = private to this file: only the three
 * functions below can read or write it. */
static telemetry_t     s_data;  /* latest value of each field          */
static vehicle_times_t s_stamp; /* when each field was last updated     */

void vehicle_state_init(void) {
    memset(&s_data, 0, sizeof(s_data));   /* every has_ flag -> false */
    memset(&s_stamp, 0, sizeof(s_stamp));
}

/* If the message carries field f: copy the value, mark it received,
 * record when. Value and time always move together. */
#define MERGE(f)                   \
do {                           \
if (msg->has_##f) {        \
s_data.f = msg->f;     \
s_data.has_##f = true; \
s_stamp.f = now_s;     \
}                          \
} while (0)

void vehicle_state_update(const telemetry_t* msg, double now_s) {
    MERGE(ts);  MERGE(fix);
    MERGE(lat); MERGE(lon); MERGE(alt); MERGE(gs);
    MERGE(ax);  MERGE(ay);  MERGE(az);
    MERGE(spd); MERGE(dist);
    MERGE(v);   MERGE(i);   MERGE(e);
    MERGE(thr); MERGE(ap);  MERGE(l1);  MERGE(l2);

    /* lts is a char array: arrays can't be assigned with '=', copy the bytes */
    if (msg->has_lts) {
        memcpy(s_data.lts, msg->lts, sizeof(s_data.lts));
        s_data.has_lts = true;
        s_stamp.lts = now_s;
    }
}

#undef MERGE

/* Age = now - time of last update, computed only when someone reads. */
#define AGE(f) ages->f = now_s - s_stamp.f

void vehicle_state_snapshot(telemetry_t* data, vehicle_times_t* ages, double now_s) {
    *data = s_data; /* struct copy: the caller gets its own version */

    AGE(ts);  AGE(fix);
    AGE(lat); AGE(lon); AGE(alt); AGE(gs);
    AGE(ax);  AGE(ay);  AGE(az);
    AGE(spd); AGE(dist);
    AGE(v);   AGE(i);   AGE(e);
    AGE(thr); AGE(ap);  AGE(l1);  AGE(l2);
    AGE(lts);
}

#undef AGE