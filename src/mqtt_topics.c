#include "mqtt_topics.h"

#include <string.h>

static telemetry_handler_t s_telemetry_handler;

void mqtt_topics_set_telemetry_handler(telemetry_handler_t handler) {
    s_telemetry_handler = handler;
}

/* uosm/car1/tlm: decode the JSON, then hand the result to whoever registered. */
static bool handle_telemetry(const char* payload, size_t len) {
    telemetry_t telemetry;
    if (!telemetry_decode(payload, len, &telemetry)) {
        return false;
    }
    if (s_telemetry_handler != NULL) {
        s_telemetry_handler(&telemetry);
    }
    return true;
}

/* THE table: one line per topic. Add the topic to docs/MQTT_TOPICS.md first. */
typedef struct {
    const char* topic;
    bool (*handle)(const char* payload, size_t len);
} topic_entry_t;

static const topic_entry_t k_topics[] = {
    { TOPIC_TELEMETRY, handle_telemetry },
};

#define TOPIC_COUNT (sizeof(k_topics) / sizeof(k_topics[0]))

bool mqtt_topics_dispatch(const char* topic, const char* payload, size_t len) {
    for (size_t k = 0; k < TOPIC_COUNT; k++) {
        if (strcmp(topic, k_topics[k].topic) == 0) {
            return k_topics[k].handle(payload, len);
        }
    }
    return false; /* unknown topic: ignored */
}
