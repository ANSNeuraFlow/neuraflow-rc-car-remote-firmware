#pragma once

#include <stddef.h>

#include "esp_err.h"

esp_err_t uart_transport_init(void);
void uart_transport_write(const char* data, size_t length);
