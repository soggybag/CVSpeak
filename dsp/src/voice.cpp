#include "cvspeak/voice.h"

#include <cmath>

namespace cvspeak {

namespace {
constexpr float kC4 = 261.6256f;
constexpr float kPlaceholderWordSeconds = 0.4f;
}  // namespace

void Voice::setSampleRate(float sampleRate) {
    sampleRate_ = sampleRate;
    envStep_ = 1.f / (kPlaceholderWordSeconds * sampleRate_);
}

void Voice::process(const VoiceInputs& in, float* out, int n, VoiceOutputs& outs) {
    outs.eow = false;

    if (in.reset) {
        env_ = 0.f;
    }
    if (in.trig) {
        // Retrigger cuts the current word and starts the next one.
        env_ = 1.f;
        phase_ = 0.f;
    }

    const float freq = kC4 * exp2f(in.pitch);
    const float dt = freq / sampleRate_;

    for (int i = 0; i < n; ++i) {
        if (env_ <= 0.f) {
            out[i] = 0.f;
            continue;
        }
        phase_ += dt;
        if (phase_ >= 1.f) phase_ -= 1.f;

        out[i] = (2.f * phase_ - 1.f) * env_ * 0.5f;

        env_ -= envStep_;
        if (env_ <= 0.f) {
            env_ = 0.f;
            outs.eow = true;
        }
    }
}

}  // namespace cvspeak
