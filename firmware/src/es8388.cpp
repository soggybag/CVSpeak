#include "es8388.h"

#include "pico/stdlib.h"

// Register values from the ES8388 datasheet (Rev 5.0, section 6).
// NOT YET TESTED ON HARDWARE: check MCLK, BCLK (2.4 MHz) and LRCK (48 kHz)
// with a scope on the breakout before trusting the rest.

namespace es8388 {

namespace {

i2c_inst_t* bus = nullptr;

bool write(uint8_t reg, uint8_t value) {
    const uint8_t buf[2] = {reg, value};
    return i2c_write_blocking(bus, kAddress, buf, 2, false) == 2;
}

struct RegValue {
    uint8_t reg, value;
};

constexpr RegValue kInit[] = {
    {0x19, 0x26},  // DAC control 3: soft ramp on, mute while configuring
    {0x08, 0x94},  // master mode; BCLK = MCLK/5 (250 MCLK per frame / 5 = 50 BCLKs)
    {0x2B, 0x80},  // DAC control 21: ADC and DAC share LRCK (QFN-28 has one LRCK pin)
    {0x00, 0x16},  // chip control 1: SameFs, reference on, VMID 500k divider
    {0x01, 0x40},  // chip control 2: analog, bias generator and reference buffer on
    {0x03, 0x58},  // ADC power: left input and left ADC on; right side and mic bias off
    {0x04, 0x30},  // DAC power: both DACs on, LOUT1 and ROUT1 on, OUT2 off
    {0x09, 0x00},  // ADC PGA 0 dB
    {0x0A, 0x00},  // ADC inputs: LIN1 / RIN1
    {0x0C, 0x0C},  // ADC: I2S, 16-bit
    {0x0D, 0x12},  // ADC: single speed, MCLK/LRCK = 250 (USB mode, 48 kHz)
    {0x0E, 0x30},  // ADC high-pass filters on (default): removes DC from Carrier In
    {0x10, 0x00},  // ADC left volume 0 dB
    {0x11, 0x00},  // ADC right volume 0 dB
    {0x12, 0x38},  // ALC off (default)
    {0x16, 0x00},  // noise gate off
    {0x17, 0x18},  // DAC: I2S, 16-bit
    {0x18, 0x12},  // DAC: single speed, MCLK/LRCK = 250
    {0x1A, 0x00},  // DAC left volume 0 dB
    {0x1B, 0x00},  // DAC right volume 0 dB
    {0x1D, 0x00},  // DAC control 7: stereo, Vpp scale default
    {0x26, 0x00},  // mixer input select (unused: LIN bypass is off)
    {0x27, 0xB8},  // left DAC -> left mixer on, LIN bypass off
    {0x2A, 0xB8},  // right DAC -> right mixer on, RIN bypass off
    {0x2D, 0x00},  // VROI 1.5k
    {0x2E, 0x1E},  // LOUT1 0 dB
    {0x2F, 0x1E},  // ROUT1 0 dB
    {0x30, 0x00},  // LOUT2 -45 dB (unused)
    {0x31, 0x00},  // ROUT2 -45 dB (unused)
    {0x02, 0x00},  // chip power: start the ADC and DAC state machines
};

}  // namespace

bool init(i2c_inst_t* i2c) {
    bus = i2c;
    if (!write(0x00, 0x80)) return false;  // reset the control port registers
    write(0x00, 0x00);
    for (const RegValue& r : kInit) {
        if (!write(r.reg, r.value)) return false;
    }
    sleep_ms(50);          // let VMID charge before unmuting
    return write(0x19, 0x22);  // unmute
}

}  // namespace es8388
