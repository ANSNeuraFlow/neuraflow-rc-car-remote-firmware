/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "actuators.h"
#include "esp_err.h"
#include "protocol.h"
#include "uart_transport.h"

void app_main(void) {
    actuators_init();
    protocol_init();
    ESP_ERROR_CHECK(uart_transport_init());
}
