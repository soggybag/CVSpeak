#pragma once
// ES8388 codec setup over I2C. See es8388.cpp for the register choices.

#include "hardware/i2c.h"

namespace es8388 {

constexpr uint8_t kAddress = 0x10;  // CE (AD0) low

// Configures the codec as I2S master at 48 kHz from a 12 MHz MCLK (USB
// mode), 16-bit, LIN1 -> left ADC, DAC -> LOUT1/ROUT1. MCLK must already be
// running. Returns false if the codec doesn't answer.
bool init(i2c_inst_t* i2c);

}  // namespace es8388
