/**
 * test_dashboard_state.c — checks dashboard_state on a PC, no Pi, no screen needed.
 *
 * vehicle_state takes the time as a parameter, so the test says "we are at 4 s"
 * without waiting 4 s.
 *
 * Build and run from the repository root (any gcc):
 *   gcc tests/test_dashboard_state.c src/dashboard_state.c src/vehicle_state.c \
 *       -Isrc -Wall -Wextra -o test_dashboard_state && ./test_dashboard_state
 */
#include "dashboard_state.h"
#include "vehicle_state.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int s_failures;
static int s_checks;

#define CHECK(cond)                                                     \
    do {                                                                \
        s_checks++;                                                     \
        if (!(cond)) {                                                  \
            s_failures++;                                               \
            printf("  FAIL line %d: %s\n", __LINE__, #cond);            \
        }                                                               \
    } while (0)

#define NEAR(a, b) (fabs((a) - (b)) < 1e-6)

int main(void) {
    dashboard_state_t st;
    telemetry_t msg;

    printf("1. Nothing received: everything invalid\n");
    vehicle_state_init();
    dashboard_state_build(&st, 0.0, false);
    CHECK(!st.speed_kmh.valid);
    CHECK(!st.distance_m.valid);
    CHECK(!st.clock_ms.valid);
    CHECK(!st.lights_valid);
    CHECK(!st.mqtt_connected);

    printf("2. Message at t = 0 s, read at t = 2 s: valid\n");
    memset(&msg, 0, sizeof(msg));
    msg.has_spd  = true; msg.spd  = 12.5;
    msg.has_dist = true; msg.dist = 1830;
    msg.has_ts   = true; msg.ts   = 1761170325000LL;
    msg.has_lts  = true; strcpy(msg.lts, "10100");
    vehicle_state_update(&msg, 0.0);

    dashboard_state_build(&st, 2.0, true);
    CHECK(st.speed_kmh.valid && NEAR(st.speed_kmh.value, 12.5));
    CHECK(st.distance_m.valid && NEAR(st.distance_m.value, 1830));
    CHECK(st.mqtt_connected);

    printf("3. The clock moves on between two messages\n");
    CHECK(st.clock_ms.valid && NEAR(st.clock_ms.value, 1761170327000.0));  /* ts + 2 s */

    printf("4. lts = \"10100\": headlight and left turn on, the rest off\n");
    CHECK(st.lights_valid);
    CHECK(st.headlight);
    CHECK(!st.running_lights);
    CHECK(st.left_turn);
    CHECK(!st.right_turn);
    CHECK(!st.wiper);

    printf("5. Read at t = 4 s (older than STALE_S): invalid, \"--\" on screen\n");
    dashboard_state_build(&st, 4.0, true);
    CHECK(!st.speed_kmh.valid);
    CHECK(!st.distance_m.valid);
    CHECK(!st.clock_ms.valid);
    CHECK(!st.lights_valid);
    CHECK(!st.headlight && !st.left_turn);  /* unknown, not shown as on */

    printf("6. Runs: nothing yet (no button)\n");
    CHECK(st.run_number == 0);
    CHECK(!st.run_time_s.valid);
    CHECK(!st.previous_runs_s[0].valid && !st.previous_runs_s[1].valid && !st.previous_runs_s[2].valid);

    printf("\n%d checks, %d failed\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
