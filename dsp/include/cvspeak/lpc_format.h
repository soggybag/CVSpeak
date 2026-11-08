#pragma once
// CVSpeak LPC bitstream format. Modeled on the TMS5220 (Speak & Spell family):
// 8 kHz, 10th-order lattice filter, 25 ms frames, variable-length frames.
// The quantization tables are CVSpeak's own (arcsine-spaced reflection
// coefficients, log-spaced energy and pitch), not TI's.
//
// Frame layout, MSB first:
//   energy  4 bits   0 = silent frame (nothing follows), 15 = stop (end of word)
//   repeat  1 bit    1 = reuse the previous frame's K coefficients
//   pitch   6 bits   0 = unvoiced (noise excitation), else a period code
//   K1..K4  5,5,4,4  present unless repeat
//   K5..K10 4,4,4,3,3,3  present only if voiced and not repeat
//
// Used by both the encoder (tools/lpc_encode) and the decoder (LpcSynth),
// so the two cannot drift apart.

#include <cstdint>

namespace cvspeak::lpc {

constexpr int kOrder = 10;
constexpr int kSampleRate = 8000;
constexpr int kFrameSamples = 200;  // 25 ms, 40 frames per second
constexpr int kUnvoicedOrder = 4;   // unvoiced frames only carry K1..K4

constexpr int kEnergyBits = 4;
constexpr int kPitchBits = 6;
constexpr int kKBits[kOrder] = {5, 5, 4, 4, 4, 4, 4, 3, 3, 3};

constexpr int kEnergySilent = 0;
constexpr int kEnergyStop = 15;

// Analysis applies pre-emphasis; synthesis undoes it with the same constant.
constexpr float kPreEmphasis = 0.9375f;

// Quantized indices for one frame, exactly as stored in the bitstream.
struct FrameCodes {
    uint8_t energy = 0;
    bool repeat = false;
    uint8_t pitch = 0;
    uint8_t k[kOrder] = {};
};

// Energy: RMS amplitude of the pre-emphasized signal, codes 1..14 log spaced.
float energyAmplitude(int code);
int quantizeEnergy(float rms);  // returns kEnergySilent for very quiet frames

// Pitch: period in samples at 8 kHz, codes 1..63 log spaced (50..500 Hz).
float pitchPeriod(int code);     // 0 for unvoiced
int quantizePitch(float period); // 0 when period <= 0

// Reflection coefficient `coef` (0-based) for a code, and the reverse.
float kValue(int coef, int code);
int quantizeK(int coef, float k);

class BitWriter {
public:
    BitWriter(uint8_t* buffer, int capacityBytes) : buf_(buffer), cap_(capacityBytes) {}

    void write(uint32_t value, int bits) {
        for (int i = bits - 1; i >= 0; --i) {
            if (pos_ / 8 >= cap_) {
                overflow_ = true;
                return;
            }
            if (pos_ % 8 == 0) buf_[pos_ / 8] = 0;
            buf_[pos_ / 8] |= static_cast<uint8_t>(((value >> i) & 1u) << (7 - pos_ % 8));
            ++pos_;
        }
    }
    int bytes() const { return (pos_ + 7) / 8; }
    bool overflow() const { return overflow_; }

private:
    uint8_t* buf_;
    int cap_;
    int pos_ = 0;
    bool overflow_ = false;
};

class BitReader {
public:
    BitReader() = default;
    BitReader(const uint8_t* data, int bytes) : data_(data), bits_(bytes * 8) {}

    // Reads past the end return zero bits and set exhausted().
    uint32_t read(int bits) {
        uint32_t v = 0;
        for (int i = 0; i < bits; ++i) {
            v <<= 1;
            if (pos_ < bits_) {
                v |= (data_[pos_ / 8] >> (7 - pos_ % 8)) & 1u;
            } else {
                exhausted_ = true;
            }
            ++pos_;
        }
        return v;
    }
    bool exhausted() const { return exhausted_; }

private:
    const uint8_t* data_ = nullptr;
    int bits_ = 0;
    int pos_ = 0;
    bool exhausted_ = false;
};

void writeFrame(BitWriter& w, const FrameCodes& f);

// Reads one frame. Only fields present in the stream are written to `f`.
// Returns false if the data ran out.
bool readFrame(BitReader& r, FrameCodes& f);

}  // namespace cvspeak::lpc
