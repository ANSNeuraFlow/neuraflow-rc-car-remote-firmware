#pragma once

#include <stdbool.h>

#include "esp_err.h"

void actuators_init(void);

bool actuators_set_throttle_level(int level);
bool actuators_set_steer_level(int level);

int actuators_get_throttle_level(void);
int actuators_get_steer_level(void);

esp_err_t actuators_cycle_lights(void);
