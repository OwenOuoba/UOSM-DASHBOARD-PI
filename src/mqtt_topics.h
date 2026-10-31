/**
 * mqtt_topics.h — routes each received MQTT message to the right decoder.
 *
 * One table entry per topic (see docs/MQTT_TOPICS.md). Today: uosm/car1/tlm.
 */
#pragma once

#include "mqtt_decode.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Telemetry V2 topic (docs/MQTT_TOPICS.md). Used by the table and by the subscription. */
#define TOPIC_TELEMETRY "uosm/car1/tlm"

/** Function that receives each decoded telemetry message (e.g. to store it). */
typedef void (*telemetry_handler_t)(const telemetry_t* telemetry);

/** Registers who receives decoded telemetry. Call once at startup. */
void mqtt_topics_set_telemetry_handler(telemetry_handler_t handler);

/**
 * Handles one MQTT message: finds the topic in the table and decodes the payload.
 * payload does not need to end with '\0'.
 *
 * Returns false if the topic is unknown or the payload invalid (both are ignored,
 * never a crash); the caller may count them for the debug screen.
 */
bool mqtt_topics_dispatch(const char* topic, const char* payload, size_t len);

#ifdef __cplusplus
}
#endif
