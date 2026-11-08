#include "oled.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

namespace oled {

namespace {

spi_inst_t* bus = nullptr;
unsigned dc = 0;

#ifdef CVSPEAK_SSD1306
constexpr uint8_t kColumnOffset = 0;
#else
// The SH1106 has 132 columns of RAM; 128-pixel modules show columns 2..129.
constexpr uint8_t kColumnOffset = 2;
#endif

void command(uint8_t c) {
    gpio_put(dc, 0);
    spi_write_blocking(bus, &c, 1);
}

}  // namespace

void init(spi_inst_t* spi, unsigned dcPin, unsigned resPin) {
    bus = spi;
    dc = dcPin;
    gpio_init(dc);
    gpio_set_dir(dc, GPIO_OUT);
    gpio_init(resPin);
    gpio_set_dir(resPin, GPIO_OUT);
    gpio_put(resPin, 0);
    sleep_ms(5);
    gpio_put(resPin, 1);
    sleep_ms(5);

    static const uint8_t kInit[] = {
        0xAE,        // display off
        0xD5, 0x80,  // clock divide / oscillator
        0xA8, 0x3F,  // multiplex 64
        0xD3, 0x00,  // display offset 0
        0x40,        // start line 0
#ifdef CVSPEAK_SSD1306
        0x20, 0x02,  // page addressing (the reset default, set anyway)
        0x8D, 0x14,  // charge pump on
#else
        0xAD, 0x8B,  // DC-DC on
#endif
        0xA1,        // segment remap (column 0 on the left)
        0xC8,        // COM scan direction: top to bottom
        0xDA, 0x12,  // COM pins
        0x81, 0x80,  // contrast
#ifdef CVSPEAK_SSD1306
        0xD9, 0xF1,  // pre-charge (internal charge pump)
#else
        0xD9, 0x22,  // pre-charge
#endif
#ifdef CVSPEAK_SSD1306
        0xDB, 0x40,  // VCOM deselect level
#else
        0xDB, 0x35,  // VCOM deselect level
        0x32,        // pump voltage 8.0 V
#endif
        0xA4,        // show RAM
        0xA6,        // not inverted
        0xAF,        // display on
    };
    for (uint8_t c : kInit) command(c);
}

void show(const uint8_t* pages) {
    for (uint8_t page = 0; page < 8; ++page) {
        command(static_cast<uint8_t>(0xB0 | page));
        command(kColumnOffset & 0x0F);          // column low nibble
        command(0x10 | (kColumnOffset >> 4));   // column high nibble
        gpio_put(dc, 1);
        spi_write_blocking(bus, pages + page * 128, 128);
    }
}

}  // namespace oled
