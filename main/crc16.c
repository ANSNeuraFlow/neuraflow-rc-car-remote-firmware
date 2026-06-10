#include "crc16.h"

uint16_t crc16_ccitt(const char* data, uint16_t length) {
    const uint16_t poly = 0x1021;
    uint16_t crc = 0xFFFF;

    for (uint16_t i = 0; i < length; i++) {
        crc ^= (uint16_t)((uint8_t)data[i]) << 8;
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) {
                crc = (uint16_t)((crc << 1) ^ poly);
            } else {
                crc <<= 1;
            }
            crc &= 0xFFFF;
        }
    }

    return crc;
}
