#pragma once
// 128x64 OLED over SPI. The finished module uses an SH1106 (1.3"); build with
// -DCVSPEAK_SSD1306=ON for an SSD1306 (0.96") module, which is handy for testing.
// The framebuffer from cvspeak::Framebuffer is already in the controllers' page order.

#include <cstdint>

#include "hardware/spi.h"

namespace oled {

void init(spi_inst_t* spi, unsigned dcPin, unsigned resPin);
void show(const uint8_t* pages);  // 8 pages x 128 bytes

}  // namespace oled
