#include "protocol.h"

#include <stdio.h>
#include <string.h>

#include "actuators.h"
#include "config.h"
#include "crc16.h"
#include "cJSON.h"
#include "esp_err.h"
#include "uart_transport.h"

static char g_work_ram[CANONICAL_JSON_MAX_LENGTH];

static const char kCrcField[] = ",\"crc\":";

static const char* find_last_crc_field(const char* line) {
    const char* last = NULL;

    for (const char* found = strstr(line, kCrcField); found != NULL;
         found = strstr(found + 1, kCrcField)) {
        last = found;
    }

    return last;
}

static bool parse_uint16(const char* digits, uint16_t* out) {
    if (digits == NULL || *digits < '0' || *digits > '9') {
        return false;
    }

    uint32_t value = 0;

    while (*digits >= '0' && *digits <= '9') {
        value = (value * 10U) + (uint32_t)(*digits - '0');
        if (value > 0xFFFFU) {
            return false;
        }
        digits++;
    }

    *out = (uint16_t)value;
    return true;
}

static bool validate_line_command_crc(const char* line, uint16_t* received_crc_out,
                                      uint16_t* calculated_crc_out) {
    const char* marker = find_last_crc_field(line);

    if (marker == NULL) {
        return false;
    }

    const size_t body_len = (size_t)(marker - line);

    if (body_len < 2 || body_len >= MAX_LINE_LENGTH || line[body_len - 1] != '}') {
        return false;
    }

    uint16_t received_crc = 0;

    if (!parse_uint16(marker + (sizeof(kCrcField) - 1), &received_crc)) {
        return false;
    }

    const uint16_t calculated_crc = crc16_ccitt(line, (uint16_t)body_len);

    if (received_crc_out != NULL) {
        *received_crc_out = received_crc;
    }

    if (calculated_crc_out != NULL) {
        *calculated_crc_out = calculated_crc;
    }

    return calculated_crc == received_crc;
}

static void write_uint16(uint16_t value) {
    char digits[6];
    uint8_t length = 0;
    char buffer[8];
    uint8_t offset = 0;

    if (value == 0) {
        buffer[offset++] = '0';
    } else {
        while (value > 0 && length < sizeof(digits)) {
            digits[length++] = (char)('0' + (value % 10));
            value /= 10;
        }
        while (length > 0) {
            buffer[offset++] = digits[--length];
        }
    }

    uart_transport_write(buffer, offset);
}

static void send_framed_message(const char* body_without_crc, uint16_t body_len) {
    if (body_without_crc == NULL || body_len < 2 ||
        body_len >= CANONICAL_JSON_MAX_LENGTH ||
        body_without_crc[body_len - 1] != '}') {
        return;
    }

    const uint16_t crc = crc16_ccitt(body_without_crc, body_len);

    uart_transport_write(body_without_crc, body_len - 1);
    uart_transport_write(",\"crc\":", 7);
    write_uint16(crc);
    uart_transport_write("}\n", 2);
}

static void send_response_ok(void) {
    const char body[] = "{\"status\":\"ok\"}";
    send_framed_message(body, (uint16_t)(sizeof(body) - 1));
}

static void send_response_ok_message(const char* message) {
    const int length = snprintf(g_work_ram, sizeof(g_work_ram),
                                "{\"message\":\"%s\",\"status\":\"ok\"}", message);

    if (length > 0 && length < (int)sizeof(g_work_ram)) {
        send_framed_message(g_work_ram, (uint16_t)length);
    }
}

static void send_response_device_info(void) {
    const int length = snprintf(
        g_work_ram, sizeof(g_work_ram),
        "{\"device\":\"rc-car\",\"firmware\":\"%s\","
        "\"lights_assumed_mode\":\"steady\",\"protocol\":%d,"
        "\"status\":\"ok\",\"steer_level_max\":%d,"
        "\"steer_level_min\":%d,\"steer_neutral_level\":0,"
        "\"throttle_above_level_max\":%d,"
        "\"throttle_forward_level_max\":%d,"
        "\"throttle_neutral_level\":0}",
        FIRMWARE_VERSION, PROTOCOL_VERSION, STEER_LEVEL_MAX, STEER_LEVEL_MIN,
        THROTTLE_ABOVE_LEVEL_MAX, THROTTLE_FORWARD_LEVEL_MAX);

    if (length > 0 && length < (int)sizeof(g_work_ram)) {
        send_framed_message(g_work_ram, (uint16_t)length);
    }
}

static void send_response_state(void) {
    const int length = snprintf(
        g_work_ram, sizeof(g_work_ram),
        "{\"status\":\"ok\",\"steer_level\":%d,\"throttle_level\":%d}",
        actuators_get_steer_level(), actuators_get_throttle_level());

    if (length > 0 && length < (int)sizeof(g_work_ram)) {
        send_framed_message(g_work_ram, (uint16_t)length);
    }
}

static void send_response_error(const char* message) {
    const int length = snprintf(g_work_ram, sizeof(g_work_ram),
                                "{\"message\":\"%s\",\"status\":\"error\"}", message);

    if (length > 0 && length < (int)sizeof(g_work_ram)) {
        send_framed_message(g_work_ram, (uint16_t)length);
    }
}

static void send_response_crc_mismatch(uint16_t received_crc,
                                       uint16_t calculated_crc) {
    const int length = snprintf(
        g_work_ram, sizeof(g_work_ram),
        "{\"calculated_crc\":%u,\"message\":\"CRC mismatch\","
        "\"received_crc\":%u,\"status\":\"error\"}",
        (unsigned)calculated_crc, (unsigned)received_crc);

    if (length > 0 && length < (int)sizeof(g_work_ram)) {
        send_framed_message(g_work_ram, (uint16_t)length);
    }
}

void protocol_send_ready_event(void) {
    const char body[] = "{\"data\":{\"device\":\"rc-car\"},\"event\":\"ready\"}";
    send_framed_message(body, (uint16_t)(sizeof(body) - 1));
}

void protocol_init(void) {}

static bool json_item_is_int_in_range(const cJSON* item, int min_value, int max_value,
                                      int* out_value) {
    if (!cJSON_IsNumber(item)) {
        return false;
    }

    const double raw = item->valuedouble;

    if (raw < (double)min_value || raw > (double)max_value) {
        return false;
    }

    if ((double)(int)raw != raw) {
        return false;
    }

    *out_value = (int)raw;
    return true;
}

static bool data_has_legacy_float_fields(const cJSON* data) {
    return cJSON_GetObjectItemCaseSensitive(data, "throttle") != NULL ||
           cJSON_GetObjectItemCaseSensitive(data, "steer") != NULL;
}

static void handle_set_controls(const cJSON* data) {
    bool has_throttle = false;
    bool has_steer = false;
    int throttle_level = 0;
    int steer_level = 0;

    if (data_has_legacy_float_fields(data)) {
        send_response_error("Invalid throttle_level");
        return;
    }

    const cJSON* throttle_item =
        cJSON_GetObjectItemCaseSensitive(data, "throttle_level");
    const cJSON* steer_item = cJSON_GetObjectItemCaseSensitive(data, "steer_level");

    if (throttle_item != NULL) {
        if (!json_item_is_int_in_range(throttle_item, THROTTLE_LEVEL_MIN,
                                       THROTTLE_LEVEL_MAX, &throttle_level)) {
            send_response_error("Invalid throttle_level");
            return;
        }
        has_throttle = true;
    }

    if (steer_item != NULL) {
        if (!json_item_is_int_in_range(steer_item, STEER_LEVEL_MIN, STEER_LEVEL_MAX,
                                       &steer_level)) {
            send_response_error("Invalid steer_level");
            return;
        }
        has_steer = true;
    }

    if (!has_throttle && !has_steer) {
        send_response_error("No controls to apply");
        return;
    }

    if (has_throttle && !actuators_set_throttle_level(throttle_level)) {
        send_response_error("Invalid throttle_level");
        return;
    }

    if (has_steer && !actuators_set_steer_level(steer_level)) {
        send_response_error("Invalid steer_level");
        return;
    }

    send_response_ok();
}

void protocol_handle_line(const char* line) {
    uint16_t received_crc = 0;
    uint16_t calculated_crc = 0;

    if (!validate_line_command_crc(line, &received_crc, &calculated_crc)) {
        send_response_crc_mismatch(received_crc, calculated_crc);
        return;
    }

    cJSON* root = cJSON_Parse(line);
    if (root == NULL) {
        send_response_error("Invalid JSON");
        return;
    }

    const cJSON* cmd_item = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    const cJSON* data_item = cJSON_GetObjectItemCaseSensitive(root, "data");

    if (!cJSON_IsString(cmd_item) || cmd_item->valuestring == NULL) {
        send_response_error("Missing cmd");
        cJSON_Delete(root);
        return;
    }

    if (!cJSON_IsObject(data_item)) {
        send_response_error("Missing data");
        cJSON_Delete(root);
        return;
    }

    const char* cmd = cmd_item->valuestring;

    if (strcmp(cmd, "heartbeat") == 0) {
        send_response_ok_message("Heartbeat");
    } else if (strcmp(cmd, "get_device_info") == 0) {
        send_response_device_info();
    } else if (strcmp(cmd, "get_state") == 0) {
        send_response_state();
    } else if (strcmp(cmd, "set_controls") == 0) {
        handle_set_controls(data_item);
    } else if (strcmp(cmd, "cycle_lights") == 0) {
        const esp_err_t err = actuators_cycle_lights();
        if (err == ESP_ERR_INVALID_STATE) {
            send_response_error("Lights pulse in progress");
        } else if (err != ESP_OK) {
            send_response_error("Lights pulse failed");
        } else {
            send_response_ok();
        }
    } else {
        send_response_error("Unknown command");
    }

    cJSON_Delete(root);
}
