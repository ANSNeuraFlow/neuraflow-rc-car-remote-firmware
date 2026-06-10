#include "uart_transport.h"

#include <string.h>

#include "config.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "protocol.h"

#define UART_PORT UART_NUM_0
#define UART_RX_BUF_SIZE 512
#define UART_TX_BUF_SIZE 512

static char s_line_buffer[MAX_LINE_LENGTH];
static size_t s_line_length = 0;

static void uart_rx_task(void* arg) {
    uint8_t byte = 0;

    (void)arg;

    while (1) {
        const int read =
            uart_read_bytes(UART_PORT, &byte, 1, pdMS_TO_TICKS(50));

        if (read <= 0) {
            continue;
        }

        if (byte == '\n' || byte == '\r') {
            if (s_line_length > 0) {
                s_line_buffer[s_line_length] = '\0';
                protocol_handle_line(s_line_buffer);
                s_line_length = 0;
            }
            continue;
        }

        if (s_line_length >= (MAX_LINE_LENGTH - 1)) {
            s_line_length = 0;
            continue;
        }

        s_line_buffer[s_line_length++] = (char)byte;
    }
}

esp_err_t uart_transport_init(void) {
    const uart_config_t uart_config = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_RX_BUF_SIZE,
                                        UART_TX_BUF_SIZE, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    s_line_length = 0;

    const BaseType_t created =
        xTaskCreate(uart_rx_task, "uart_rx", 4096, NULL, 5, NULL);

    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void uart_transport_write(const char* data, size_t length) {
    if (data == NULL || length == 0) {
        return;
    }

    uart_write_bytes(UART_PORT, data, length);
}
