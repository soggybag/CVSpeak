#include "cvspeak/lpc_synth.h"

#include <cmath>

namespace cvspeak {

namespace {

// Voiced excitation = impulse + chirpAmount * chirp. The chirp is a short
// decaying downward sweep in the spirit of the TMS5220's chirp ROM (not a
// copy of it) and adds a buzzy, metallic edge. On its own it lacks low end
// (words come out 3-9 dB quiet and thin), so the impulse carries the body.
constexpr int kChirpLen = 32;

struct ChirpTable {
    float v[kChirpLen];
    ChirpTable() {
        float phase = 0.f;
        for (int n = 0; n < kChirpLen; ++n) {
            float f = 0.30f - 0.008f * static_cast<float>(n);  // cycles per sample
            if (f < 0.03f) f = 0.03f;
            v[n] = std::cos(phase) * std::exp(-static_cast<float>(n) / 7.f);
            phase += 6.2831853f * f;
        }
    }
};

const ChirpTable kChirp;

constexpr float kMinHz = 30.f;
constexpr float kMaxHz = 2000.f;

}  // namespace

void LpcSynth::start(const LpcWord& word) {
    word_ = &word;
    reader_ = lpc::BitReader(word.data, word.bytes);
    lattice_.reset();
    deemph_ = dcX_ = dcY_ = 0.f;
    nextIsStop_ = false;
    t_ = 0.f;
    cur_ = Params{};
    next_ = Params{};

    // Start directly on frame 0 so a trigger speaks immediately.
    decodeNext();
    cur_ = next_;
    if (nextIsStop_) {
        word_ = nullptr;
        return;
    }
    decodeNext();

    if (pulseEnergy_ <= 0.f) setChirp(chirpAmount_);
    pulsePhase_ = 1e9f;  // first voiced sample fires a pulse
    pulsePos_ = kChirpLen;
}

void LpcSynth::setSpeed(float ratio) {
    speed_ = ratio < 0.05f ? 0.05f : ratio;
}

void LpcSynth::setChirp(float amount) {
    amount = amount < 0.f ? 0.f : (amount > 1.f ? 1.f : amount);
    if (amount == chirpAmount_ && pulseEnergy_ > 0.f) return;
    chirpAmount_ = amount;
    pulseEnergy_ = 0.f;
    for (int n = 0; n < kChirpLen; ++n) {
        const float v = (n == 0 ? 1.f : 0.f) + chirpAmount_ * kChirp.v[n];
        pulseEnergy_ += v * v;
    }
}

void LpcSynth::setPitch(float hz, float intonation) {
    pitchHz_ = hz;
    intonation_ = intonation < 0.f ? 0.f : (intonation > 1.f ? 1.f : intonation);
}

void LpcSynth::decodeNext() {
    lpc::FrameCodes f;
    if (!lpc::readFrame(reader_, f) || f.energy == lpc::kEnergyStop) {
        next_ = cur_;
        next_.energy = 0.f;  // fade out into the stop
        nextIsStop_ = true;
        return;
    }

    Params p = cur_;  // silent and repeat frames inherit the previous filter
    if (f.energy == lpc::kEnergySilent) {
        p.energy = 0.f;
        next_ = p;
        return;
    }
    p.energy = lpc::energyAmplitude(f.energy);
    p.voiced = f.pitch != 0;
    p.period = lpc::pitchPeriod(f.pitch);
    if (!f.repeat) {
        for (int i = 0; i < lpc::kOrder; ++i) {
            p.k[i] = (i < lpc::kUnvoicedOrder || p.voiced) ? lpc::kValue(i, f.k[i]) : 0.f;
        }
    }
    next_ = p;
}

float LpcSynth::tick(bool& ended) {
    ended = false;
    if (!word_) return 0.f;

    const float a = t_;
    float k[lpc::kOrder];
    float gainSq = 1.f;
    for (int i = 0; i < lpc::kOrder; ++i) {
        k[i] = cur_.k[i] + (next_.k[i] - cur_.k[i]) * a;
        gainSq *= 1.f - k[i] * k[i];
    }
    const float energy = cur_.energy + (next_.energy - cur_.energy) * a;
    const bool voiced = a < 0.5f ? cur_.voiced : next_.voiced;

    // Excitation, normalized to unit power so `energy` sets the output RMS.
    float e;
    if (voiced) {
        float period;
        if (cur_.voiced && next_.voiced) {
            period = cur_.period + (next_.period - cur_.period) * a;
        } else {
            period = cur_.voiced ? cur_.period : next_.period;
        }
        const float ratio = word_->medianPeriod / period;  // > 1 above the word's median
        float hz = pitchHz_ * std::exp2(intonation_ * std::log2(ratio));
        hz = hz < kMinHz ? kMinHz : (hz > kMaxHz ? kMaxHz : hz);
        const float periodSamples = static_cast<float>(lpc::kSampleRate) / hz;

        pulsePhase_ += 1.f;
        if (pulsePhase_ >= periodSamples) {
            pulsePhase_ -= periodSamples;
            if (pulsePhase_ >= periodSamples) pulsePhase_ = 0.f;
            pulsePos_ = 0;
            pulseScale_ = std::sqrt(periodSamples / pulseEnergy_);
        }
        if (pulsePos_ < kChirpLen) {
            e = ((pulsePos_ == 0 ? 1.f : 0.f) + chirpAmount_ * kChirp.v[pulsePos_]) * pulseScale_;
            ++pulsePos_;
        } else {
            e = 0.f;
        }
    } else {
        noise_ ^= noise_ << 13;
        noise_ ^= noise_ >> 17;
        noise_ ^= noise_ << 5;
        e = (noise_ & 1u) ? 1.f : -1.f;
    }

    float y = lattice_.process(e * energy * std::sqrt(gainSq), k);
    deemph_ = y + lpc::kPreEmphasis * deemph_;
    y = deemph_;

    // DC blocker (~13 Hz): de-emphasis has 24 dB of gain at DC.
    const float dc = y - dcX_ + 0.99f * dcY_;
    dcX_ = y;
    dcY_ = dc;

    t_ += speed_ / static_cast<float>(lpc::kFrameSamples);
    while (t_ >= 1.f) {
        t_ -= 1.f;
        if (nextIsStop_) {
            ended = true;
            word_ = nullptr;
            break;
        }
        cur_ = next_;
        decodeNext();
    }
    return dc;
}

}  // namespace cvspeak
