#pragma once
// Panel and CV inputs on core 0: the two 74HC4051 muxes, V/OCT on its own
// ADC pin, and the encoder.
//
// Build with -DCVSPEAK_BREADBOARD=ON for a bare Pico 2 without the analog
// stages: knobs and CVs then sit at their defaults, and BACK, SPEAK and the
// encoder push are buttons to ground on the spare pins GP15, GP20 and GP22.

namespace controls {

struct Readings {
    // Pots, 0..1 (fully clockwise = 1).
    float pitch = 0.25f, speed = 0.5f, formant = 0.5f, bend = 0.f, blend = 1.f, intonation = 1.f;
    // Jack voltages.
    float voct = 0.f, selectCv = 0.f, speedCv = 0.f, formantCv = 0.f, bendCv = 0.f;
    bool back = false, encoderPush = false, speak = false;
    bool carrierPatched = false;
};

void init();
void scan(Readings& r);  // call about every 1 ms
int takeEncoderDetents();

}  // namespace controls
