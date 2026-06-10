#include "actuators.h"

#include "config.h"
#include "driver/dac_oneshot.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static dac_oneshot_handle_t s_throttle_handle;
static dac_oneshot_handle_t s_steer_handle;
static uint8_t s_throttle_code = THROTTLE_CODE_START;
static uint8_t s_steer_code = STEER_CODE_START;
static int s_throttle_level = 0;
static int s_steer_level = 0;
static bool s_lights_pulse_active = false;

static uint8_t clamp_code(int code, uint8_t min, uint8_t max) {
    if (code < (int)min) {
        return min;
    }
    if (code > (int)max) {
        return max;
    }
    return (uint8_t)code;
}

static bool throttle_level_in_range(int level) {
    return level >= THROTTLE_LEVEL_MIN && level <= THROTTLE_LEVEL_MAX;
}

static bool steer_level_in_range(int level) {
    return level >= STEER_LEVEL_MIN && level <= STEER_LEVEL_MAX;
}

static uint8_t throttle_level_to_code(int level) {
    const int code = THROTTLE_CODE_START - level;
    return clamp_code(code, THROTTLE_CODE_MIN, THROTTLE_CODE_MAX);
}

static uint8_t steer_level_to_code(int level) {
    const int code = STEER_CODE_CENTER + level;
    return clamp_code(code, STEER_CODE_MIN, STEER_CODE_MAX);
}

static void apply_throttle(void) {
    ESP_ERROR_CHECK(
        dac_oneshot_output_voltage(s_throttle_handle, s_throttle_code));
}

static void apply_steer(void) {
    ESP_ERROR_CHECK(dac_oneshot_output_voltage(s_steer_handle, s_steer_code));
}

static void init_lights_gpio(void) {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << LIGHTS_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(LIGHTS_GPIO, LIGHTS_IDLE_LEVEL);
}

void actuators_init(void) {
    dac_oneshot_config_t throttle_cfg = {
        .chan_id = DAC_CHAN_0,
    };
    ESP_ERROR_CHECK(dac_oneshot_new_channel(&throttle_cfg, &s_throttle_handle));

    dac_oneshot_config_t steer_cfg = {
        .chan_id = DAC_CHAN_1,
    };
    ESP_ERROR_CHECK(dac_oneshot_new_channel(&steer_cfg, &s_steer_handle));

    init_lights_gpio();
    apply_throttle();
    apply_steer();
}

bool actuators_set_throttle_level(int level) {
    if (!throttle_level_in_range(level)) {
        return false;
    }

    s_throttle_level = level;
    s_throttle_code = throttle_level_to_code(level);
    apply_throttle();
    return true;
}

bool actuators_set_steer_level(int level) {
    if (!steer_level_in_range(level)) {
        return false;
    }

    s_steer_level = level;
    s_steer_code = steer_level_to_code(level);
    apply_steer();
    return true;
}

int actuators_get_throttle_level(void) {
    return s_throttle_level;
}

int actuators_get_steer_level(void) {
    return s_steer_level;
}

esp_err_t actuators_cycle_lights(void) {
    if (s_lights_pulse_active) {
        return ESP_ERR_INVALID_STATE;
    }

    s_lights_pulse_active = true;
    gpio_set_level(LIGHTS_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(LIGHTS_PULSE_HIGH_MS));
    gpio_set_level(LIGHTS_GPIO, LIGHTS_IDLE_LEVEL);
    s_lights_pulse_active = false;

    return ESP_OK;
}
