/**
* display.h — terminal display of the vehicle state.
 *
 * Prints one line per call with every telemetry field:
 *   never received -> name=---
 *   older than 2 s -> name=value(age s)
 *   fresh          -> name=value
 */
#ifndef DISPLAY_H
#define DISPLAY_H

/* Take a snapshot of the vehicle state and print it on one line. */
void display_print(double now_s);

#endif /* DISPLAY_H */