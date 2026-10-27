/**
 * mqtt_decode.h — decodes the Telemetry V2 JSON message (topic uosm/car1/tlm).
 *
 * Format and units: Telemetry V2 MQTT Interface Specification v1.0, summarised in
 * docs/MQTT_TOPICS.md.
 *
 * Every field comes with a has_<name> flag: false when the field is missing or
 * invalid. The specification omits a field when its sensor has no valid value
 * that second, so a missing field is normal, not an error.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool has_ts;   int64_t ts;   /* ms, UTC epoch, from the GPS */
    bool has_lat;  double lat;   /* degrees */
    bool has_lon;  double lon;   /* degrees */
    bool has_alt;  double alt;   /* m */
    bool has_gs;   double gs;    /* km/h, speed from the GPS */
    bool has_fix;  int fix;      /* 0 none, 1 GPS, 2 DGPS, 4 RTK fixed, 5 RTK float */
    bool has_ax;   double ax;    /* g, mean over the second */
    bool has_ay;   double ay;    /* g */
    bool has_az;   double az;    /* g */
    bool has_spd;  double spd;   /* km/h, Hall-effect speedometer */
    bool has_dist; double dist;  /* m, distance since start */
    bool has_v;    double v;     /* V, mean over the second */
    bool has_i;    double i;     /* A, mean over the second */
    bool has_e;    double e;     /* J consumed during the second (= average power in W) */
    bool has_thr;  double thr;   /* %, throttle 0-100 */
    bool has_lts;  char lts[6];  /* "11000": headlight, running lights, left, right, wiper */
    bool has_ap;   double ap;    /* kPa (unit to confirm in the specification) */
    bool has_l1;   double l1;    /* kg (unit to confirm) */
    bool has_l2;   double l2;    /* kg (unit to confirm) */
} telemetry_t;

/**
 * Decodes one uosm/car1/tlm payload into *out.
 *
 * Fields are looked up by name; unknown fields are ignored; a field with the
 * wrong type is treated as missing. payload does not need to end with '\0'.
 *
 * Returns false (and leaves every has_ flag false) if the payload is not a JSON
 * object. Never crashes on bad input.
 */
bool telemetry_decode(const char* payload, size_t len, telemetry_t* out);

#ifdef __cplusplus
}
#endif
