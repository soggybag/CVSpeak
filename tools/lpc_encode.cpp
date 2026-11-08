// Encodes 8 kHz 16-bit mono WAV files into the CVSpeak LPC bitstream and
// writes them out as a C++ word bank.
//   usage: lpc_encode out.cpp NAME=file.wav [NAME=file.wav ...]
// Normally run by tools/make_words.sh (`make words`).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "cvspeak/lpc_analysis.h"
#include "cvspeak/lpc_format.h"
#include "wav.h"

using namespace cvspeak;
using namespace cvspeak::lpc;

namespace {

constexpr int kLpcWindow = 300;     // 37.5 ms Hamming window per frame
constexpr int kPitchWindow = 400;   // 50 ms, long enough for a 50 Hz period
constexpr float kVoicedMinDb = -42.f;
constexpr float kPeakTarget = 0.89f;  // normalize every word to -1 dBFS

struct Encoded {
    std::string name;
    std::vector<uint8_t> bytes;
    int frames = 0;
    int voicedFrames = 0;
    float medianPeriod = 0.f;
    int minEnergy = 99, maxEnergy = 0;
};

// Copies `len` samples centered on `center` (zero outside the signal).
std::vector<float> segment(const std::vector<float>& s, int center, int len) {
    std::vector<float> out(len, 0.f);
    const int start = center - len / 2;
    for (int i = 0; i < len; ++i) {
        const int j = start + i;
        if (j >= 0 && j < static_cast<int>(s.size())) out[i] = s[j];
    }
    return out;
}

Encoded encodeWord(const std::string& name, std::vector<float> x) {
    float peak = 0.f;
    for (float v : x) peak = std::max(peak, std::fabs(v));
    if (peak > 0.f)
        for (float& v : x) v *= kPeakTarget / peak;

    std::vector<float> pre(x.size());
    for (size_t n = 0; n < x.size(); ++n) pre[n] = x[n] - kPreEmphasis * (n ? x[n - 1] : 0.f);

    // Two cascaded one-pole low-passes (~900 Hz) clean up pitch detection.
    std::vector<float> lp(x.size());
    const float a = std::exp(-6.2831853f * 900.f / static_cast<float>(kSampleRate));
    float z1 = 0.f, z2 = 0.f;
    for (size_t n = 0; n < x.size(); ++n) {
        z1 = x[n] + a * (z1 - x[n]);
        z2 = z1 + a * (z2 - z1);
        lp[n] = z2;
    }

    std::vector<float> hamming(kLpcWindow);
    for (int i = 0; i < kLpcWindow; ++i)
        hamming[i] = 0.54f - 0.46f * std::cos(6.2831853f * static_cast<float>(i) / (kLpcWindow - 1));

    const int numFrames = static_cast<int>((x.size() + kFrameSamples - 1) / kFrameSamples);
    struct Raw {
        float rms = 0.f;
        float k[kOrder] = {};
        float period = 0.f;
        float corr = 0.f;
        float zcr = 0.f;
        bool voiced = false;
    };
    std::vector<Raw> raw(numFrames);

    for (int m = 0; m < numFrames; ++m) {
        const int center = m * kFrameSamples + kFrameSamples / 2;

        std::vector<float> w = segment(pre, center, kLpcWindow);
        for (int i = 0; i < kLpcWindow; ++i) w[i] *= hamming[i];
        float r[kOrder + 1];
        autocorrelate(w.data(), kLpcWindow, r, kOrder);
        r[0] = r[0] * 1.0001f + 1e-9f;  // white-noise correction for conditioning
        levinson(r, kOrder, raw[m].k);

        float sum = 0.f;
        int count = 0, crossings = 0;
        for (int i = m * kFrameSamples; i < (m + 1) * kFrameSamples && i < static_cast<int>(pre.size()); ++i) {
            sum += pre[i] * pre[i];
            if (i > 0 && (x[i] >= 0.f) != (x[i - 1] >= 0.f)) ++crossings;
            ++count;
        }
        raw[m].rms = count ? std::sqrt(sum / static_cast<float>(count)) : 0.f;
        raw[m].zcr = count ? static_cast<float>(crossings) / static_cast<float>(count) : 0.f;

        std::vector<float> p = segment(lp, center, kPitchWindow);
        raw[m].period = detectPitch(p.data(), kPitchWindow, 16, 160, 0.f, &raw[m].corr);
    }

    int peakEnergy = 0;
    for (const Raw& r : raw) peakEnergy = std::max(peakEnergy, quantizeEnergy(r.rms));
    for (Raw& r : raw) {
        VoicingFeatures f;
        f.corr = r.corr;
        f.k1 = r.k[0];
        f.zcr = r.zcr;
        f.belowPeak = peakEnergy - quantizeEnergy(r.rms);
        const float db = r.rms > 0.f ? 20.f * std::log10(r.rms) : -200.f;
        r.voiced = classifyVoiced(f) && db > kVoicedMinDb && r.period > 0.f;
    }

    // Remove single-frame voicing flips, then median-3 the pitch track.
    for (int m = 1; m + 1 < numFrames; ++m) {
        if (raw[m - 1].voiced == raw[m + 1].voiced && raw[m].voiced != raw[m - 1].voiced) {
            raw[m].voiced = raw[m - 1].voiced;
            if (raw[m].voiced && raw[m].period <= 0.f)
                raw[m].period = 0.5f * (raw[m - 1].period + raw[m + 1].period);
        }
    }
    // Fix octave errors (creaky voice often correlates best at 2x the period):
    // pull each period to within ~0.7..1.4x of its voiced neighbours' median.
    for (int m = 0; m < numFrames; ++m) {
        if (!raw[m].voiced) continue;
        std::vector<float> near;
        for (int d = -2; d <= 2; ++d) {
            const int j = m + d;
            if (d != 0 && j >= 0 && j < numFrames && raw[j].voiced) near.push_back(raw[j].period);
        }
        if (near.empty()) continue;
        std::sort(near.begin(), near.end());
        const float ref = near[near.size() / 2];
        while (raw[m].period > 1.7f * ref && raw[m].period / 2.f >= 16.f) raw[m].period /= 2.f;
        while (raw[m].period < 0.6f * ref && raw[m].period * 2.f <= 160.f) raw[m].period *= 2.f;
    }

    std::vector<float> smoothed(numFrames, 0.f);
    for (int m = 0; m < numFrames; ++m) {
        if (!raw[m].voiced) continue;
        std::vector<float> win;
        for (int d = -1; d <= 1; ++d) {
            const int j = m + d;
            if (j >= 0 && j < numFrames && raw[j].voiced) win.push_back(raw[j].period);
        }
        std::sort(win.begin(), win.end());
        smoothed[m] = win[win.size() / 2];
    }

    std::vector<FrameCodes> codes(numFrames);
    for (int m = 0; m < numFrames; ++m) {
        FrameCodes& f = codes[m];
        f.energy = static_cast<uint8_t>(quantizeEnergy(raw[m].rms));
        f.pitch = raw[m].voiced ? static_cast<uint8_t>(quantizePitch(smoothed[m])) : 0;
        for (int i = 0; i < kOrder; ++i) f.k[i] = static_cast<uint8_t>(quantizeK(i, raw[m].k[i]));
    }

    // Trim leading and trailing silence so triggers speak immediately.
    int first = 0, last = numFrames - 1;
    while (first <= last && codes[first].energy == kEnergySilent) ++first;
    while (last >= first && codes[last].energy == kEnergySilent) --last;

    Encoded out;
    out.name = name;
    std::vector<uint8_t> buffer(16 * numFrames + 16);
    BitWriter writer(buffer.data(), static_cast<int>(buffer.size()));
    const FrameCodes* prevCoded = nullptr;
    std::vector<float> voicedPeriods;

    for (int m = first; m <= last; ++m) {
        FrameCodes f = codes[m];
        if (f.energy != kEnergySilent) {
            out.minEnergy = std::min<int>(out.minEnergy, f.energy);
            out.maxEnergy = std::max<int>(out.maxEnergy, f.energy);
            // Repeat when the filter barely changed: saves the K bits.
            if (prevCoded && (prevCoded->pitch != 0) == (f.pitch != 0)) {
                const int n = f.pitch ? kOrder : kUnvoicedOrder;
                int diff = 0;
                for (int i = 0; i < n; ++i) diff += std::abs(f.k[i] - prevCoded->k[i]);
                if (diff <= 1) {
                    f.repeat = true;
                    for (int i = 0; i < kOrder; ++i) f.k[i] = prevCoded->k[i];
                }
            }
            if (!f.repeat) prevCoded = &codes[m];
            if (f.pitch) {
                ++out.voicedFrames;
                voicedPeriods.push_back(pitchPeriod(f.pitch));
            }
        }
        writeFrame(writer, f);
        ++out.frames;
    }
    FrameCodes stop;
    stop.energy = kEnergyStop;
    writeFrame(writer, stop);
    if (writer.overflow()) std::fprintf(stderr, "%s: bitstream buffer overflow\n", name.c_str());
    out.bytes.assign(buffer.begin(), buffer.begin() + writer.bytes());

    if (voicedPeriods.empty()) {
        out.medianPeriod = 64.f;  // 125 Hz, only used for unvoiced-only words
    } else {
        std::sort(voicedPeriods.begin(), voicedPeriods.end());
        out.medianPeriod = voicedPeriods[voicedPeriods.size() / 2];
    }
    return out;
}

bool writeBank(const char* path, const std::vector<Encoded>& words) {
    FILE* f = std::fopen(path, "w");
    if (!f) return false;
    std::fprintf(f,
                 "// GENERATED by tools/lpc_encode from tools/words.txt (`make words`). Do not edit.\n"
                 "#include \"cvspeak/word_bank.h\"\n\nnamespace cvspeak {\n\nnamespace {\n");
    for (size_t w = 0; w < words.size(); ++w) {
        std::fprintf(f, "\n// %s: %d frames, %zu bytes\nconst uint8_t kWord%zu[] = {", words[w].name.c_str(),
                     words[w].frames, words[w].bytes.size(), w);
        for (size_t i = 0; i < words[w].bytes.size(); ++i)
            std::fprintf(f, "%s0x%02x,", i % 16 ? " " : "\n    ", words[w].bytes[i]);
        std::fprintf(f, "\n};\n");
    }
    std::fprintf(f, "\n}  // namespace\n\nconst LpcWord kWords[] = {\n");
    for (size_t w = 0; w < words.size(); ++w)
        std::fprintf(f, "    {\"%s\", kWord%zu, sizeof(kWord%zu), %d, %.2ff},\n", words[w].name.c_str(), w, w,
                     words[w].frames, static_cast<double>(words[w].medianPeriod));
    std::fprintf(f, "};\n\nconst int kNumWords = %zu;\n\n}  // namespace cvspeak\n", words.size());
    std::fclose(f);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s out.cpp NAME=file.wav [...]\n", argv[0]);
        return 2;
    }
    std::vector<Encoded> words;
    size_t totalBytes = 0;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        const size_t eq = arg.find('=');
        if (eq == std::string::npos) {
            std::fprintf(stderr, "bad argument (want NAME=file.wav): %s\n", argv[i]);
            return 2;
        }
        const std::string name = arg.substr(0, eq), path = arg.substr(eq + 1);
        std::vector<float> samples;
        int rate = 0;
        if (!tools::readWav16(path.c_str(), samples, rate)) {
            std::fprintf(stderr, "%s: not a 16-bit mono PCM WAV\n", path.c_str());
            return 1;
        }
        if (rate != kSampleRate) {
            std::fprintf(stderr, "%s: sample rate %d, need %d\n", path.c_str(), rate, kSampleRate);
            return 1;
        }
        words.push_back(encodeWord(name, samples));
        const Encoded& e = words.back();
        totalBytes += e.bytes.size();
        std::printf("%-12s %3d frames %4zu bytes  voiced %3d%%  median %5.1f Hz  energy %d..%d\n",
                    e.name.c_str(), e.frames, e.bytes.size(), e.frames ? 100 * e.voicedFrames / e.frames : 0,
                    static_cast<double>(kSampleRate / e.medianPeriod), e.minEnergy, e.maxEnergy);
    }
    if (!writeBank(argv[1], words)) {
        std::fprintf(stderr, "could not write %s\n", argv[1]);
        return 1;
    }
    std::printf("wrote %s: %zu words, %zu bytes\n", argv[1], words.size(), totalBytes);
    return 0;
}
