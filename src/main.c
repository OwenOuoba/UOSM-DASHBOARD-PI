#include "mqtt_topics.h"
#include "vehicle_state.h"
#include "display.h"
#include "dashboard_state.h"
#include "screen.h"

#include <mosquitto.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Main loop timing (see docs/DASHBOARD.md, "Option A: one loop, one thread") */
#define LOOP_TIMEOUT_MS  5     /* mosquitto_loop waits at most this long for network data */
#define IDLE_SLEEP_MS   10     /* pause when there is no connection, so the loop does not spin */
#define RECONNECT_S    2.0     /* time between two connection attempts */
#define PRINT_PERIOD_S 1.0     /* terminal line period, independent of messages */
#define SCREEN_PERIOD_S 0.1    /* screen refresh from the state (ui.h: every 100 ms) */
#define KEEPALIVE_S      5     /* MQTT keep-alive: a dead link is detected in ~5-10 s (libmosquitto minimum) */

static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void sleep_ms(long ms) {
    struct timespec t = {0, ms * 1000000L};
    nanosleep(&t, NULL);
}

/* Set to 0 by Ctrl+C (SIGINT) or systemd stop (SIGTERM): the loop ends cleanly. */
static volatile sig_atomic_t s_running = 1;

static void on_signal(int sig) {
    (void)sig;
    s_running = 0;
}

/* Connected to the broker? Shown on every terminal line, later on the screen. */
static bool s_mqtt_connected = false;

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

    vehicle_state_update(t, now_s());
    /* No printing here: the main loop prints at a fixed rate (PRINT_PERIOD_S) */
}

/* Called when the connection to the broker is established (also after a
 * reconnection): subscribing here means we never lose the subscription. */
static void on_connect(struct mosquitto* mosq, void* userdata, int rc) {
    (void)userdata;
    if (rc != 0) {
        fprintf(stderr, "Broker refused the connection: %s\n", mosquitto_connack_string(rc));
        return;
    }
    s_mqtt_connected = true;

    /* QoS 1, as required by the Telemetry V2 specification */
    printf("Connected to broker, subscribing to %s\n", TOPIC_TELEMETRY);
    int sub_rc = mosquitto_subscribe(mosq, NULL, TOPIC_TELEMETRY, 1);
    if (sub_rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Subscribe to %s failed: %s\n", TOPIC_TELEMETRY, mosquitto_strerror(sub_rc));
    }
}

/* Called when the connection is lost (rc != 0) or closed by us (rc == 0). */
static void on_disconnect(struct mosquitto* mosq, void* userdata, int rc) {
    (void)mosq;
    (void)userdata;
    s_mqtt_connected = false;
    if (rc != 0) {
        printf("Disconnected from broker, retrying every %.0f s\n", RECONNECT_S);
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
    /* Give the broker as an IP address: resolving a host name (DNS) can block the loop */
    const char* host = (argc > 1) ? argv[1] : "localhost";
    int port = (argc > 2) ? atoi(argv[2]) : 1883;

    /* Print each line immediately, even when the output is redirected (e.g. to a log) */
    setvbuf(stdout, NULL, _IOLBF, 0);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    vehicle_state_init();
    mqtt_topics_set_telemetry_handler(on_telemetry);

    /* No screen (unplugged, no access to /dev/fb0) is not fatal: the terminal keeps working. */
    bool screen_ok = screen_init();
    if (!screen_ok) {
        fprintf(stderr, "No screen, terminal only\n");
    }

    mosquitto_lib_init();

    struct mosquitto* mosq = mosquitto_new("uosm-dashboard", true, NULL);
    if (mosq == NULL) {
        fprintf(stderr, "mosquitto_new failed\n");
        mosquitto_lib_cleanup();
        return 1;
    }
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_disconnect_callback_set(mosq, on_disconnect);
    mosquitto_message_callback_set(mosq, on_message);

    /* _async: starts the connection and returns at once; mosquitto_loop() finishes it.
     * A broker that is down is no longer fatal: the loop keeps retrying. */
    printf("Connecting to %s:%d\n", host, port);
    int rc = mosquitto_connect_async(mosq, host, port, KEEPALIVE_S);
    bool connect_started = (rc == MOSQ_ERR_SUCCESS);
    if (!connect_started) {
        fprintf(stderr, "Cannot reach the broker: %s, retrying every %.0f s\n",
                mosquitto_strerror(rc), RECONNECT_S);
    }

    /* Option A: one loop, one thread. Nothing in it may block for long,
     * so network, terminal (and later the screen) all keep running. */
    double next_retry = now_s() + RECONNECT_S;
    double next_print = 0;
    double next_screen = 0;

    while (s_running) {
        double now = now_s();

        /* 1. Network: waits at most LOOP_TIMEOUT_MS, runs the callbacks here,
         *    in this thread, and sends the keep-alive to the broker. */
        rc = mosquitto_loop(mosq, LOOP_TIMEOUT_MS, 1);
        if (rc != MOSQ_ERR_SUCCESS) {
            if (now >= next_retry) {
                next_retry = now + RECONNECT_S;
                /* reconnect needs a first successful connect_async (it reuses its host and port) */
                rc = connect_started ? mosquitto_reconnect_async(mosq)
                                     : mosquitto_connect_async(mosq, host, port, KEEPALIVE_S);
                if (rc == MOSQ_ERR_SUCCESS) {
                    connect_started = true;
                }
            }
            sleep_ms(IDLE_SLEEP_MS);   /* without a connection, mosquitto_loop returns at once */
        }

        /* 2. Terminal: one line per PRINT_PERIOD_S, even when nothing arrives,
         *    so the growing ages show a silent telemetry. */
        if (now >= next_print) {
            next_print = now + PRINT_PERIOD_S;
            if (!s_mqtt_connected) {
                printf("[no broker] ");
            }
            display_print(now);
        }

        /* 3. Screen: every SCREEN_PERIOD_S, state -> dashboard_state_t -> ui_update();
         *    every turn, LVGL draws what changed. */
        if (screen_ok) {
            if (now >= next_screen) {
                next_screen = now + SCREEN_PERIOD_S;
                dashboard_state_t st;
                dashboard_state_build(&st, now, s_mqtt_connected);
                screen_update(&st);
            }
            screen_tick();
        }
    }

    printf("Stopping\n");
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    return 0;
}
