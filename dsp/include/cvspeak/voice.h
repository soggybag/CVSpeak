#pragma once
// CVSpeak DSP core — shared by the VCV Rack plugin, the RP2350 firmware and
// the desktop test tools. Rules for everything under dsp/:
//   * plain C++17, no Rack / Pico SDK / OS headers
//   * float only (the Cortex-M33 FPU is single precision; no double math)
//   * no heap allocation inside process()

namespace cvspeak {

// Control state for one processing block. Edge detection (Schmitt triggers,
// debouncing) is the wrapper's job; the core only receives clean events.
struct VoiceInputs {
    float pitch = 0.f;    // volts, 1V/oct, 0 V = C4 (VCV Rack convention)
    bool trig = false;    // rising edge occurred at the start of this block
    bool reset = false;   // rising edge occurred at the start of this block
};

struct VoiceOutputs {
    bool eow = false;     // a word finished during this block
};

class Voice {
public:
    void setSampleRate(float sampleRate);

    // Render n samples in [-1, 1] into out. Events in `in` apply at sample 0.
    void process(const VoiceInputs& in, float* out, int n, VoiceOutputs& outs);

    bool speaking() const { return env_ > 0.f; }

private:
    // Phase 0 placeholder: a decaying sawtooth "word". Replaced by the LPC
    // engine in Phase 1; the interface above is what stays stable.
    float sampleRate_ = 48000.f;
    float phase_ = 0.f;
    float env_ = 0.f;
    float envStep_ = 0.f;
};

}  // namespace cvspeak
