#pragma once
// Audio on core 1: I2S with the ES8388, the Voice, gates and EOW.

namespace audio {

constexpr float kSampleRate = 48000.f;  // 12 MHz MCLK / 250

// Starts the I2S state machine and launches core 1. The codec must be
// configured first (it is the I2S clock master).
void start();

}  // namespace audio
