idf_component_register(SRCS "main.c" "actuators.c" "protocol.c"
                            "uart_transport.c" "crc16.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_driver_dac esp_driver_gpio esp_driver_uart json)
