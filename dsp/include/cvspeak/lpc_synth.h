#pragma once
// LPC speech decoder running at 8 kHz: bitstream -> interpolated parameters
// -> chirp/noise excitation -> 10-pole lattice filter -> de-emphasis.

#include <cstdint>

#include "cvspeak/lpc_format.h"
#include "cvspeak/word_bank.h"

namespace cvspeak {

// All-pole lattice synthesis filter, 1/A(z), driven by reflection coefficients.
// Stable for any |k| < 1, including while the coefficients are interpolated.
class LatticeFilter {
public:
    void reset() {
        for (float& v : b_) v = 0.f;
    }
    float process(float excitation, const float* k) {
        float f = excitation;
        for (int i = lpc::kOrder - 1; i >= 0; --i) {
            f -= k[i] * b_[i];
            b_[i + 1] = b_[i] + k[i] * f;
        }
        b_[0] = f;
        return f;
    }

private:
    float b_[lpc::kOrder + 1] = {};
};

class LpcSynth {
public:
    // Starts speaking `word` from its first frame, cutting off any current word.
    void start(const LpcWord& word);
    void stop() { word_ = nullptr; }
    bool active() const { return word_ != nullptr; }

    // Frame rate multiplier: 1 = as recorded, 2 = twice as fast. Pitch is unaffected.
    void setSpeed(float ratio);

    // `hz` is where the word's median pitch lands. `intonation` 0 = monotone at
    // `hz`, 1 = the recorded pitch contour transposed to `hz`.
    void setPitch(float hz, float intonation);

    // Voiced excitation character: 0 = clean impulse, 1 = strong TI-style chirp.
    void setChirp(float amount);

    // Renders one 8 kHz sample. Sets `ended` on the sample the word finishes.
    float tick(bool& ended);

private:
    struct Params {
        float energy = 0.f;
        bool voiced = false;
        float period = 0.f;
        float k[lpc::kOrder] = {};
    };

    void decodeNext();

    const LpcWord* word_ = nullptr;
    lpc::BitReader reader_;
    Params cur_, next_;
    bool nextIsStop_ = false;
    float t_ = 0.f;  // position from cur_ (0) to next_ (1)

    float speed_ = 1.f;
    float pitchHz_ = 130.81f;
    float intonation_ = 1.f;
    float chirpAmount_ = 0.5f;
    float pulseEnergy_ = 0.f;  // energy of one excitation pulse, for normalization

    float pulsePhase_ = 0.f;
    float pulseScale_ = 0.f;
    int pulsePos_ = 0;
    uint32_t noise_ = 0x1234567u;

    LatticeFilter lattice_;
    float deemph_ = 0.f;
    float dcX_ = 0.f, dcY_ = 0.f;
};

}  // namespace cvspeak
