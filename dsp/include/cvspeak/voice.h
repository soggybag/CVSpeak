#pragma once
// CVSpeak DSP core — shared by the VCV Rack plugin, the RP2350 firmware and
// the desktop test tools. Rules for everything under dsp/:
//   * plain C++17, no Rack / Pico SDK / OS headers
//   * float only (the Cortex-M33 FPU is single precision; no double math)
//   * no heap allocation inside process()

#include "cvspeak/lpc_synth.h"
#include "cvspeak/sequencer.h"

namespace cvspeak {

// Control state for one processing block. Edge detection (Schmitt triggers,
// debouncing) is the wrapper's job; the core only receives clean events.
struct VoiceInputs {
    float pitch = -1.f;      // volts, 1V/oct, 0 V = C4 (VCV Rack convention)
    float speed = 1.f;       // frame rate multiplier, independent of pitch
    float intonation = 1.f;  // 0 = monotone, 1 = recorded pitch contour
    float chirp = 0.5f;      // 0 = clean impulse, 1 = TI-style chirp excitation
    float formant = 0.f;     // octaves of formant shift, -1.5..1.5 (pitch and speed unaffected)
    float bend = 0.f;        // circuit bending, 0..1: bit flips and frame stutters
    const float* carrier = nullptr;  // n samples of external excitation (~±1), or null if unpatched
    float blend = 1.f;       // with a carrier: 0 = internal buzz, 1 = carrier only
    Mode mode = Mode::Word;
    int word = 0;            // Word mode: index into kWords, latched on trig
    int phrase = 0;          // Phrase mode: index into kPhrases
    bool trig = false;       // rising edge: speak (cuts off the current word)
    bool reset = false;      // rising edge: next trig speaks the phrase's first word.
                             // Applied before trig, so both at once speak word 1.
};

struct VoiceOutputs {
    bool eow = false;        // a word finished during this block
};

class Voice {
public:
    void setSampleRate(float sampleRate);

    // Render n samples into out, limited to [-1, 1]. Events in `in` apply at sample 0.
    void process(const VoiceInputs& in, float* out, int n, VoiceOutputs& outs);

    bool speaking() const { return synth_.active(); }

    // Output saturation: identity up to 0.7, then a smooth tanh curve that
    // approaches but never reaches +/-1.
    static float softClip(float x);
    int currentWord() const { return word_; }  // last word triggered, -1 if none
    const PhraseSequencer& sequencer() const { return sequencer_; }

private:
    void setFormant(float ratio);

    // RBJ low-pass biquad, removes images left by the 8 kHz -> host rate step.
    struct Biquad {
        float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
        float x1 = 0.f, x2 = 0.f, y1 = 0.f, y2 = 0.f;
        void setLowpass(float cutoffHz, float sampleRate);
        float process(float x);
    };

    LpcSynth synth_;
    PhraseSequencer sequencer_;
    Biquad lowpass_;
    float sampleRate_ = 48000.f;
    float formantRatio_ = 1.f;
    float step_ = 8000.f / 48000.f;  // synth ticks per host sample
    float carrierSum_ = 0.f;
    int carrierCount_ = 0;
    float carrierLast_ = 0.f;
    float carrierPower_ = 0.f;
    float carrierCoef_ = 0.f;
    float limiterGain_ = 1.f;
    float limiterRelease_ = 0.f;
    float frac_ = 0.f;
    float s0_ = 0.f, s1_ = 0.f;      // last two synth samples
    int word_ = -1;
};

}  // namespace cvspeak
