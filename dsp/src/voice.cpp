#include "cvspeak/voice.h"

#include <cmath>

namespace cvspeak {

namespace {
constexpr float kC4 = 261.6256f;
constexpr float kImageCutoffHz = 3600.f;
}  // namespace

void Voice::Biquad::setLowpass(float cutoffHz, float sampleRate) {
    if (cutoffHz > 0.45f * sampleRate) cutoffHz = 0.45f * sampleRate;
    const float w0 = 6.2831853f * cutoffHz / sampleRate;
    const float alpha = std::sin(w0) / (2.f * 0.7071f);
    const float cosw = std::cos(w0);
    const float a0 = 1.f + alpha;
    b0 = (1.f - cosw) * 0.5f / a0;
    b1 = (1.f - cosw) / a0;
    b2 = b0;
    a1 = -2.f * cosw / a0;
    a2 = (1.f - alpha) / a0;
}

float Voice::Biquad::process(float x) {
    const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
}

void Voice::setSampleRate(float sampleRate) {
    sampleRate_ = sampleRate;
    step_ = static_cast<float>(lpc::kSampleRate) / sampleRate_;
    lowpass_.setLowpass(kImageCutoffHz, sampleRate_);
}

void Voice::process(const VoiceInputs& in, float* out, int n, VoiceOutputs& outs) {
    outs.eow = false;

    if (in.reset) synth_.stop();
    if (in.trig && kNumWords > 0) {
        // Retrigger cuts the current word and starts the next one.
        int w = in.word;
        if (w < 0) w = 0;
        if (w >= kNumWords) w = kNumWords - 1;
        word_ = w;
        synth_.start(kWords[w]);
    }
    synth_.setSpeed(in.speed);
    synth_.setChirp(in.chirp);
    synth_.setPitch(kC4 * std::exp2(in.pitch), in.intonation);

    for (int i = 0; i < n; ++i) {
        frac_ += step_;
        while (frac_ >= 1.f) {
            frac_ -= 1.f;
            bool ended = false;
            s0_ = s1_;
            s1_ = synth_.tick(ended);
            if (ended) outs.eow = true;
        }
        out[i] = lowpass_.process(s0_ + (s1_ - s0_) * frac_);
    }
}

}  // namespace cvspeak
