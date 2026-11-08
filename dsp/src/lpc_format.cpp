#include "cvspeak/lpc_format.h"

#include <cmath>

namespace cvspeak::lpc {

namespace {

// Largest |k| each coefficient can represent. Low-order coefficients swing
// close to +/-1 in voiced speech; higher ones stay smaller.
constexpr float kKMax[kOrder] = {0.985f, 0.985f, 0.9f, 0.9f, 0.8f,
                                 0.8f,   0.8f,   0.7f, 0.7f, 0.7f};

constexpr float kEnergyMinDb = -54.f;
constexpr float kEnergyMaxDb = -6.f;
constexpr int kEnergyLevels = 14;  // codes 1..14

constexpr float kPitchMinPeriod = 16.f;   // 500 Hz
constexpr float kPitchMaxPeriod = 160.f;  // 50 Hz
constexpr int kPitchLevels = 63;          // codes 1..63

float energyStepDb() { return (kEnergyMaxDb - kEnergyMinDb) / (kEnergyLevels - 1); }

float kStep(int coef) {
    const int levels = 1 << kKBits[coef];
    return 2.f * std::asin(kKMax[coef]) / static_cast<float>(levels - 1);
}

}  // namespace

float energyAmplitude(int code) {
    if (code <= kEnergySilent || code >= kEnergyStop) return 0.f;
    const float db = kEnergyMinDb + static_cast<float>(code - 1) * energyStepDb();
    return std::pow(10.f, db / 20.f);
}

int quantizeEnergy(float rms) {
    if (rms <= 0.f) return kEnergySilent;
    const float db = 20.f * std::log10(rms);
    int code = 1 + static_cast<int>(std::lround((db - kEnergyMinDb) / energyStepDb()));
    if (code < 1) return kEnergySilent;
    if (code > kEnergyLevels) code = kEnergyLevels;
    return code;
}

float pitchPeriod(int code) {
    if (code <= 0) return 0.f;
    const float t = static_cast<float>(code - 1) / static_cast<float>(kPitchLevels - 1);
    return kPitchMinPeriod * std::pow(kPitchMaxPeriod / kPitchMinPeriod, t);
}

int quantizePitch(float period) {
    if (period <= 0.f) return 0;
    const float t = std::log(period / kPitchMinPeriod) / std::log(kPitchMaxPeriod / kPitchMinPeriod);
    int code = 1 + static_cast<int>(std::lround(t * static_cast<float>(kPitchLevels - 1)));
    if (code < 1) code = 1;
    if (code > kPitchLevels) code = kPitchLevels;
    return code;
}

float kValue(int coef, int code) {
    const float theta = -std::asin(kKMax[coef]) + static_cast<float>(code) * kStep(coef);
    return std::sin(theta);
}

int quantizeK(int coef, float k) {
    const float kmax = kKMax[coef];
    if (k > kmax) k = kmax;
    if (k < -kmax) k = -kmax;
    const int code = static_cast<int>(std::lround((std::asin(k) + std::asin(kmax)) / kStep(coef)));
    const int maxCode = (1 << kKBits[coef]) - 1;
    return code < 0 ? 0 : (code > maxCode ? maxCode : code);
}

void writeFrame(BitWriter& w, const FrameCodes& f) {
    w.write(f.energy, kEnergyBits);
    if (f.energy == kEnergySilent || f.energy == kEnergyStop) return;
    w.write(f.repeat ? 1u : 0u, 1);
    w.write(f.pitch, kPitchBits);
    if (f.repeat) return;
    const int n = f.pitch ? kOrder : kUnvoicedOrder;
    for (int i = 0; i < n; ++i) w.write(f.k[i], kKBits[i]);
}

bool readFrame(BitReader& r, FrameCodes& f) {
    f.energy = static_cast<uint8_t>(r.read(kEnergyBits));
    f.repeat = false;
    if (f.energy != kEnergySilent && f.energy != kEnergyStop) {
        f.repeat = r.read(1) != 0;
        f.pitch = static_cast<uint8_t>(r.read(kPitchBits));
        if (!f.repeat) {
            const int n = f.pitch ? kOrder : kUnvoicedOrder;
            for (int i = 0; i < n; ++i) f.k[i] = static_cast<uint8_t>(r.read(kKBits[i]));
        }
    }
    return !r.exhausted();
}

}  // namespace cvspeak::lpc
