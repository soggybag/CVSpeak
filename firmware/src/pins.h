#pragma once
// Pico 2 pin assignment. Source of truth: hardware/docs/Pin map.md.

namespace pins {

constexpr unsigned kI2cSda = 0;     // I2C0: ES8388 control
constexpr unsigned kI2cScl = 1;
constexpr unsigned kOledSck = 2;    // SPI0: SH1106 OLED (CS tied low)
constexpr unsigned kOledMosi = 3;
constexpr unsigned kOledDc = 4;
constexpr unsigned kOledRes = 5;
constexpr unsigned kEncA = 6;       // internal pull-ups
constexpr unsigned kEncB = 7;
constexpr unsigned kGateTrig = 8;   // inverted by the MMBT3904 stage
constexpr unsigned kGateReset = 9;
constexpr unsigned kEowOut = 10;
constexpr unsigned kLedSpeak = 11;
constexpr unsigned kMuxS0 = 12;     // both 74HC4051s
constexpr unsigned kMuxS1 = 13;
constexpr unsigned kMuxS2 = 14;
constexpr unsigned kI2sDout = 16;   // RP2350 -> ES8388 DSDIN
constexpr unsigned kI2sDin = 17;    // ES8388 ASDOUT -> RP2350. The PIO program needs
constexpr unsigned kI2sBclk = 18;   //   DIN, BCLK and LRCK on consecutive pins.
constexpr unsigned kI2sLrck = 19;
constexpr unsigned kCodecMclk = 21; // GPOUT0: 12 MHz from the crystal
constexpr unsigned kAdcVoct = 26;   // ADC0
constexpr unsigned kAdcMuxA = 27;   // ADC1: pots, BACK, encoder push
constexpr unsigned kAdcMuxB = 28;   // ADC2: CVs, SPEAK, Carrier detect

// ADC input numbers (GPIO 26 + n).
constexpr unsigned kAdcInVoct = 0;
constexpr unsigned kAdcInMuxA = 1;
constexpr unsigned kAdcInMuxB = 2;

// Mux channels.
namespace muxA {
constexpr int kPitch = 0, kSpeed = 1, kFormant = 2, kBend = 3, kBlend = 4, kIntonation = 5, kBack = 6,
              kEncPush = 7;
}
namespace muxB {
constexpr int kSelectCv = 0, kSpeedCv = 1, kFormantCv = 2, kBendCv = 3, kSpeak = 4, kCarrierDetect = 5;
}

}  // namespace pins
