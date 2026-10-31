/**
* display.h — terminal display of the vehicle state.
 *
 * Prints one line per call with every telemetry field:
 *   never received -> name=---
 *   stale          -> name=value(age s)   (older than STALE_S, vehicle_state.h)
 *   fresh          -> name=value
 */
#ifndef DISPLAY_H
#define DISPLAY_H

/* Take a snapshot of the vehicle state and print it on one line. */
void display_print(double now_s);

#endif /* DISPLAY_H */