#include "cvspeak/voice.h"

#include <cmath>

namespace cvspeak {

namespace {
constexpr float kC4 = 261.6256f;
constexpr float kImageCutoffHz = 3600.f;  // at Formant 0; scales with the formant ratio
constexpr float kMaxFormantOctaves = 1.5f;
constexpr float kCarrierFloor = 1e-4f;     // -40 dB: quieter carriers aren't boosted further
constexpr float kCarrierSeconds = 0.02f;   // carrier level follower time constant
constexpr float kLimiterRelease = 0.05f;   // seconds for the safety limiter to recover
constexpr float kLimiterThreshold = 3.f;   // only extreme spikes (heavy Bend) reach it
constexpr float kClipKnee = 0.7f;          // soft clipping starts here
}  // namespace

float Voice::softClip(float x) {
    const float mag = std::fabs(x);
    if (mag <= kClipKnee) return x;
    // tanh curve from the knee towards 1.0. Its slope at the knee is 1, so
    // there is no corner where clipping starts.
    const float room = 1.f - kClipKnee;
    const float y = kClipKnee + room * std::tanh((mag - kClipKnee) / room);
    return x < 0.f ? -y : y;
}

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
    limiterRelease_ = 1.f - std::exp(-1.f / (kLimiterRelease * sampleRate_));
    formantRatio_ = -1.f;  // force setFormant to recompute
    setFormant(1.f);
}

void Voice::setFormant(float ratio) {
    if (std::fabs(ratio - formantRatio_) < 1e-4f) return;
    formantRatio_ = ratio;
    const float synthRate = static_cast<float>(lpc::kSampleRate) * ratio;
    synth_.setRate(synthRate);
    step_ = synthRate / sampleRate_;
    lowpass_.setLowpass(kImageCutoffHz * ratio, sampleRate_);
    carrierCoef_ = 1.f - std::exp(-1.f / (kCarrierSeconds * synthRate));
}

void Voice::process(const VoiceInputs& in, float* out, int n, VoiceOutputs& outs) {
    outs.eow = false;

    if (in.reset) sequencer_.reset();
    if (in.trig && kNumWords > 0) {
        int w = in.mode == Mode::Phrase ? sequencer_.next(in.phrase) : in.word;
        if (w >= 0) {
            if (w >= kNumWords) w = kNumWords - 1;
            word_ = w;
            synth_.start(kWords[w]);  // cuts off the current word
        }
    }

    float formant = in.formant;
    if (formant > kMaxFormantOctaves) formant = kMaxFormantOctaves;
    if (formant < -kMaxFormantOctaves) formant = -kMaxFormantOctaves;
    setFormant(std::exp2(formant));

    synth_.setSpeed(in.speed);
    synth_.setChirp(in.chirp);
    synth_.setBend(in.bend);
    synth_.setPitch(kC4 * std::exp2(in.pitch), in.intonation);

    float blend = in.carrier ? in.blend : 0.f;
    blend = blend < 0.f ? 0.f : (blend > 1.f ? 1.f : blend);

    for (int i = 0; i < n; ++i) {
        if (in.carrier) {
            carrierSum_ += in.carrier[i];
            ++carrierCount_;
        }
        frac_ += step_;
        while (frac_ >= 1.f) {
            frac_ -= 1.f;

            // Carrier: average the host samples since the last tick (a crude
            // decimator; the 8 kHz-ish synthesis is lo-fi anyway), then
            // normalize to unit power so any patch level talks equally loud.
            float c = 0.f;
            if (in.carrier) {
                if (carrierCount_ > 0) {
                    c = carrierSum_ / static_cast<float>(carrierCount_);
                    carrierLast_ = c;
                } else {
                    c = carrierLast_;
                }
                carrierSum_ = 0.f;
                carrierCount_ = 0;
                carrierPower_ += (c * c - carrierPower_) * carrierCoef_;
                const float p = carrierPower_ > kCarrierFloor ? carrierPower_ : kCarrierFloor;
                c /= std::sqrt(p);
            }
            synth_.setCarrier(c, blend);

            bool ended = false;
            s0_ = s1_;
            s1_ = synth_.tick(ended);
            if (ended) outs.eow = true;
        }
        float y = lowpass_.process(s0_ + (s1_ - s0_) * frac_);

        // Output stage. Overloads (some words peak at ~1.3x; Bend spikes to
        // 16x+) are soft clipped into gentle saturation or crunch rather than
        // ducked. A safety limiter first tames extreme spikes, which would
        // otherwise saturate into a harsh square wave. Output stays within +/-1.
        const float mag = std::fabs(y);
        if (mag * limiterGain_ > kLimiterThreshold) limiterGain_ = kLimiterThreshold / mag;
        out[i] = softClip(y * limiterGain_);
        limiterGain_ += (1.f - limiterGain_) * limiterRelease_;
    }
}

}  // namespace cvspeak
