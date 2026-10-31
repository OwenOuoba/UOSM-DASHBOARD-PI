#include "mqtt_topics.h"
#include "vehicle_state.h"
#include "display.h"

#include <mosquitto.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* Newest GPS timestamp (ts) accepted so far. */
static int64_t s_last_ts = INT64_MIN;

static void on_telemetry(const telemetry_t* t) {
    /* QoS 1 may deliver a message twice, and a late message must never replace
     * a newer one: a message whose ts is not newer than the last one is ignored.
     * Messages without ts are accepted (no way to compare them). */
    if (t->has_ts) {
        if (t->ts <= s_last_ts) {
            return;
        }
        s_last_ts = t->ts;
    }

    double now = now_s();
    vehicle_state_update(t, now);
    display_print(now);
}

/* Called when the connection to the broker is established (also after a
 * reconnection): subscribing here means we never lose the subscription. */
static void on_connect(struct mosquitto* mosq, void* userdata, int rc) {
    (void)userdata;
    if (rc != 0) {
        fprintf(stderr, "Broker refused the connection: %s\n", mosquitto_connack_string(rc));
        return;
    }
    /* QoS 1, as required by the Telemetry V2 specification */
    printf("Connected to broker, subscribing to %s\n", TOPIC_TELEMETRY);
    int sub_rc = mosquitto_subscribe(mosq, NULL, TOPIC_TELEMETRY, 1);
    if (sub_rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Subscribe to %s failed: %s\n", TOPIC_TELEMETRY, mosquitto_strerror(sub_rc));
    }
}

/* Called for every received message: same entry point as the STM32 transports. */
static void on_message(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* msg) {
    (void)mosq;
    (void)userdata;

    /* Retained message: the last sample the broker kept, sent right after we
     * subscribe. It can be hours old but would get an age of 0 here, so it is
     * never shown as live. Cost: "--" for at most 1 s at startup (1 Hz). */
    if (msg->retain) {
        return;
    }

    const char* payload = msg->payload ? (const char*)msg->payload : "";
    mqtt_topics_dispatch(msg->topic, payload, (size_t)msg->payloadlen);
}

int main(int argc, char** argv) {
    const char* host = (argc > 1) ? argv[1] : "localhost";
    int port = (argc > 2) ? atoi(argv[2]) : 1883;

    /* Print each line immediately, even when the output is redirected (e.g. to a log) */
    setvbuf(stdout, NULL, _IOLBF, 0);

    vehicle_state_init();
    mqtt_topics_set_telemetry_handler(on_telemetry);

    mosquitto_lib_init();

    struct mosquitto* mosq = mosquitto_new("uosm-dashboard", true, NULL);
    if (mosq == NULL) {
        fprintf(stderr, "mosquitto_new failed\n");
        return 1;
    }
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);

    printf("Connecting to %s:%d\n", host, port);
    int rc = mosquitto_connect(mosq, host, port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Cannot reach the broker: %s\n", mosquitto_strerror(rc));
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return 1;
    }

    /* Runs forever: receives messages and reconnects automatically if the broker drops */
    mosquitto_loop_forever(mosq, -1, 1);

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    return 0;
}