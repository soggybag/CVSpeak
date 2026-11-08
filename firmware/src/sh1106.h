#pragma once
// SH1106 128x64 OLED over SPI (1.3" modules). The framebuffer from
// cvspeak::Framebuffer is already in the controller's page order.

#include <cstdint>

#include "hardware/spi.h"

namespace sh1106 {

void init(spi_inst_t* spi, unsigned dcPin, unsigned resPin);
void show(const uint8_t* pages);  // 8 pages x 128 bytes

}  // namespace sh1106
