/**
 * test_decode.c — checks mqtt_topics + mqtt_decode on a PC, no Pi, no MQTT needed.
 *
 * Build and run from the repository root (any gcc):
 *   gcc tests/test_decode.c src/mqtt_topics.c src/mqtt_decode.c lib/cjson/cJSON.c \
 *       -Isrc -Ilib/cjson -Wall -Wextra -o test_decode && ./test_decode
 */
#include "mqtt_topics.h"

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

#define NEAR(a, b) (fabs((a) - (b)) < 1e-9)

static int s_handler_calls;
static telemetry_t s_last;

static void on_telemetry(const telemetry_t* t) {
    s_handler_calls++;
    s_last = *t;
}

static bool decode(const char* json, telemetry_t* t) {
    return telemetry_decode(json, strlen(json), t);
}

int main(void) {
    telemetry_t t;

    printf("1. Full example message from the specification\n");
    CHECK(decode("{\"ts\":1761170325123,\"lat\":45.42153,\"lon\":-75.697193,\"alt\":70.2,"
                 "\"gs\":24.1,\"fix\":4,\"ax\":0.02,\"ay\":-0.01,\"az\":1,\"spd\":24.5,"
                 "\"dist\":1830,\"v\":47.02,\"i\":3.15,\"e\":148,\"thr\":62,\"lts\":\"11000\","
                 "\"ap\":101.3,\"l1\":35.2,\"l2\":34.8}", &t));
    CHECK(t.has_ts && t.ts == 1761170325123LL);
    CHECK(t.has_spd && NEAR(t.spd, 24.5));
    CHECK(t.has_v && NEAR(t.v, 47.02));
    CHECK(t.has_i && NEAR(t.i, 3.15));
    CHECK(t.has_e && NEAR(t.e, 148));
    CHECK(t.has_fix && t.fix == 4);
    CHECK(t.has_lts && strcmp(t.lts, "11000") == 0);
    CHECK(t.has_l2 && NEAR(t.l2, 34.8));

    printf("2. Partial message (sensors without a valid value are omitted)\n");
    CHECK(decode("{\"ts\":1761170325123,\"v\":47.02,\"i\":3.15}", &t));
    CHECK(t.has_v && !t.has_spd && !t.has_lts && !t.has_lat);

    printf("3. Wrong types and invalid lights are treated as missing\n");
    CHECK(decode("{\"spd\":\"abc\",\"v\":47.02,\"lts\":\"1100\"}", &t));
    CHECK(!t.has_spd && t.has_v && !t.has_lts);
    CHECK(decode("{\"lts\":\"11a00\"}", &t) && !t.has_lts);

    printf("4. Unknown fields are ignored\n");
    CHECK(decode("{\"foo\":1,\"spd\":30}", &t) && t.has_spd && NEAR(t.spd, 30));

    printf("5. Invalid payloads are rejected, never a crash\n");
    CHECK(!decode("not json", &t) && !t.has_spd);
    CHECK(!decode("[1,2,3]", &t));
    CHECK(!decode("{\"spd\":24.5", &t));
    CHECK(!telemetry_decode("", 0, &t));

    printf("6. Payload not '\\0'-terminated: only len bytes are read\n");
    const char raw[] = "{\"spd\":12}GARBAGE";
    CHECK(telemetry_decode(raw, 10, &t) && t.has_spd && NEAR(t.spd, 12));

    printf("7. Dispatch: right topic reaches the handler, others are ignored\n");
    mqtt_topics_set_telemetry_handler(on_telemetry);
    const char msg[] = "{\"spd\":42}";
    CHECK(mqtt_topics_dispatch("uosm/car1/tlm", msg, strlen(msg)));
    CHECK(s_handler_calls == 1 && s_last.has_spd && NEAR(s_last.spd, 42));
    CHECK(!mqtt_topics_dispatch("uosm/car2/tlm", msg, strlen(msg)));
    CHECK(!mqtt_topics_dispatch("uosm/car1/tlm", "bad", 3));
    CHECK(s_handler_calls == 1);

    printf("\n%d checks, %d failed\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
