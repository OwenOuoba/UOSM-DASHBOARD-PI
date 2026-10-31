#include "display.h"
#include "vehicle_state.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

/* A field older than STALE_S (vehicle_state.h) is shown with its age. */
static void print_age_if_stale(double age) {
    if (age > STALE_S) {
        printf("(%.0fs)", age);
    }
}

/* has_ is checked first: the age of a never-received field is meaningless. */
static void print_num(const char* name, bool has, double value, double age, int decimals) {
    if (!has) {
        printf(" %s=---", name);
        return;
    }
    printf(" %s=%.*f", name, decimals, value);
    print_age_if_stale(age);
}

void display_print(double now_s) {
    telemetry_t d;
    vehicle_times_t a;
    vehicle_state_snapshot(&d, &a, now_s);

    /* ts and fix are integers: printed separately */
    if (d.has_ts) {
        printf("ts=%" PRId64, d.ts);
        print_age_if_stale(a.ts);
    } else {
        printf("ts=---");
    }

    if (d.has_fix) {
        printf(" fix=%d", d.fix);
        print_age_if_stale(a.fix);
    } else {
        printf(" fix=---");
    }

    print_num("lat",  d.has_lat,  d.lat,  a.lat,  6);
    print_num("lon",  d.has_lon,  d.lon,  a.lon,  6);
    print_num("alt",  d.has_alt,  d.alt,  a.alt,  1);
    print_num("gs",   d.has_gs,   d.gs,   a.gs,   1);
    print_num("ax",   d.has_ax,   d.ax,   a.ax,   2);
    print_num("ay",   d.has_ay,   d.ay,   a.ay,   2);
    print_num("az",   d.has_az,   d.az,   a.az,   2);
    print_num("spd",  d.has_spd,  d.spd,  a.spd,  1);
    print_num("dist", d.has_dist, d.dist, a.dist, 2);
    print_num("v",    d.has_v,    d.v,    a.v,    2);
    print_num("i",    d.has_i,    d.i,    a.i,    2);
    print_num("e",    d.has_e,    d.e,    a.e,    2);
    print_num("thr",  d.has_thr,  d.thr,  a.thr,  2);
    print_num("ap",   d.has_ap,   d.ap,   a.ap,   2);
    print_num("l1",   d.has_l1,   d.l1,   a.l1,   2);
    print_num("l2",   d.has_l2,   d.l2,   a.l2,   2);

    /* lts is a string */
    if (d.has_lts) {
        printf(" lts=%s", d.lts);
        print_age_if_stale(a.lts);
    } else {
        printf(" lts=---");
    }

    printf("\n");
}