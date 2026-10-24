#include "mqtt_decode.h"

#include "cJSON.h"

#include <string.h>

/* Reads a number field. Returns false if the field is missing or not a number. */
static bool get_number(const cJSON* root, const char* name, double* out) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    *out = item->valuedouble;
    return true;
}

/* lts must be exactly 5 characters, each '0' or '1' (specification section 0.3.3). */
static bool get_lights(const cJSON* root, char out[6]) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, "lts");
    if (!cJSON_IsString(item) || strlen(item->valuestring) != 5) {
        return false;
    }
    for (int k = 0; k < 5; k++) {
        if (item->valuestring[k] != '0' && item->valuestring[k] != '1') {
            return false;
        }
    }
    memcpy(out, item->valuestring, 6); /* 5 characters + '\0' */
    return true;
}

bool telemetry_decode(const char* payload, size_t len, telemetry_t* out) {
    memset(out, 0, sizeof(*out)); /* every has_ flag starts false */

    /* ParseWithLength: the MQTT payload is not '\0'-terminated, we must respect len */
    cJSON* root = cJSON_ParseWithLength(payload, len);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root); /* safe on NULL */
        return false;
    }

    double tmp;
    if (get_number(root, "ts", &tmp))  { out->has_ts = true;  out->ts = (int64_t)tmp; }
    if (get_number(root, "fix", &tmp)) { out->has_fix = true; out->fix = (int)tmp; }

    out->has_lat  = get_number(root, "lat",  &out->lat);
    out->has_lon  = get_number(root, "lon",  &out->lon);
    out->has_alt  = get_number(root, "alt",  &out->alt);
    out->has_gs   = get_number(root, "gs",   &out->gs);
    out->has_ax   = get_number(root, "ax",   &out->ax);
    out->has_ay   = get_number(root, "ay",   &out->ay);
    out->has_az   = get_number(root, "az",   &out->az);
    out->has_spd  = get_number(root, "spd",  &out->spd);
    out->has_dist = get_number(root, "dist", &out->dist);
    out->has_v    = get_number(root, "v",    &out->v);
    out->has_i    = get_number(root, "i",    &out->i);
    out->has_e    = get_number(root, "e",    &out->e);
    out->has_thr  = get_number(root, "thr",  &out->thr);
    out->has_ap   = get_number(root, "ap",   &out->ap);
    out->has_l1   = get_number(root, "l1",   &out->l1);
    out->has_l2   = get_number(root, "l2",   &out->l2);
    out->has_lts  = get_lights(root, out->lts);

    cJSON_Delete(root); /* cJSON allocates memory while parsing: always free it */
    return true;
}
